#include "chunk_fragment_reader.h"

#include "private.h"

#include "chunk_reader_options.h"
#include "chunk_replica_locator.h"
#include "config.h"
#include "data_node_service_proxy.h"
#include "dispatcher.h"

#include <yt/yt/ytlib/chunk_client/proto/data_node_service.pb.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>
#include <yt/yt/ytlib/node_tracker_client/channel.h>

#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/connection.h>

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/concurrency/delayed_executor.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/misc/assert.h>
#include <yt/yt/core/misc/async_expiring_cache.h>
#include <yt/yt/core/misc/intrusive_ptr.h>
#include <yt/yt/core/misc/ref_counted.h>
#include <yt/yt/core/misc/sync_expiring_cache.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/rpc/helpers.h>

namespace NYT::NChunkClient {

using namespace NApi::NNative;
using namespace NConcurrency;
using namespace NNodeTrackerClient;
using namespace NRpc;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

namespace {

// TODO(akozhikhov): Drop this after chunk replica cache.
using TChunkReplicaLocatorCache = TSyncExpiringCache<TChunkId, TChunkReplicaLocatorPtr>;
using TChunkReplicaLocatorCachePtr = TIntrusivePtr<TChunkReplicaLocatorCache>;

////////////////////////////////////////////////////////////////////////////////

struct TPeerInfo
{
    TAddressWithNetwork Address;
    IChannelPtr Channel;
};

using TPeerInfoCache = TSyncExpiringCache<TNodeId, TErrorOr<TPeerInfo>>;
using TPeerInfoCachePtr = TIntrusivePtr<TPeerInfoCache>;

////////////////////////////////////////////////////////////////////////////////

using TProbeChunkSetResult = TDataNodeServiceProxy::TRspProbeChunkSetPtr;
using TErrorOrProbeChunkSetResult = TDataNodeServiceProxy::TErrorOrRspProbeChunkSetPtr;

}  // namespace

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TChunkFragmentReader)

class TChunkFragmentReader
    : public IChunkFragmentReader
{
public:
    TChunkFragmentReader(
        TChunkFragmentReaderConfigPtr config,
        IClientPtr client,
        INodeStatusDirectoryPtr nodeStatusDirectory)
        : Config_(std::move(config))
        , Client_(std::move(client))
        , NodeDirectory_(Client_->GetNativeConnection()->GetNodeDirectory())
        , NodeStatusDirectory_(std::move(nodeStatusDirectory))
        , Networks_(Client_->GetNativeConnection()->GetNetworks())
        , Logger(ChunkClientLogger.WithTag("ChunkFragmentReaderId: %v", TGuid::Create()))
        , ReaderInvoker_(TDispatcher::Get()->GetReaderInvoker())
        , ChunkReplicaLocatorCache_(New<TChunkReplicaLocatorCache>(BIND([
                logger = Logger,
                client = Client_,
                nodeDirectory = NodeDirectory_,
                config = Config_
            ] (TChunkId chunkId) {
                return New<TChunkReplicaLocator>(
                    client,
                    nodeDirectory,
                    chunkId,
                    config->SeedsExpirationTimeout,
                    TChunkReplicaList{},
                    logger.WithTag("ChunkId: %v", chunkId));
            }),
            Config_->ChunkReplicaLocatorExpirationTimeout,
            ReaderInvoker_))
        , PeerInfoCache_(New<TPeerInfoCache>(
            BIND([this_ = MakeWeak(this)] (TNodeId nodeId) -> TErrorOr<TPeerInfo> {
                auto reader = this_.Lock();
                if (!reader) {
                    return TError(NYT::EErrorCode::Canceled, "Reader was destroyed");
                }

                return reader->GetPeerInfo(nodeId);
            }),
            Config_->PeerInfoExpirationTimeout,
            ReaderInvoker_))
    {
        SchedulePeriodicUpdate();
    }

    virtual TFuture<std::vector<TSharedRef>> ReadFragments(
        TClientChunkReadOptions options,
        std::vector<TChunkFragmentRequest> requests) override;

private:
    struct TPeerAccessInfo
        : public TPeerInfo
    {
        YT_DECLARE_SPINLOCK(TAdaptiveLock, Lock);
        TNodeId NodeId;
        TInstant LastSuccessfulAccessTime;
    };

    class TSessionBase;
    class TReadFragmentsSession;
    class TPeriodicUpdateSession;

    const TChunkFragmentReaderConfigPtr Config_;
    const IClientPtr Client_;
    const TNodeDirectoryPtr NodeDirectory_;
    const INodeStatusDirectoryPtr NodeStatusDirectory_;
    const TNetworkPreferenceList Networks_;

    const NLogging::TLogger Logger;

    const IInvokerPtr ReaderInvoker_;

    const TChunkReplicaLocatorCachePtr ChunkReplicaLocatorCache_;
    const TPeerInfoCachePtr PeerInfoCache_;

    // TODO(akozhikhov): Implement lock sharding.
    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, ChunkIdToPeerAccessInfoLock_);
    // NB: It is used for fast path and eviction of obsolete chunks.
    THashMap<TChunkId, TPeerAccessInfo> ChunkIdToPeerAccessInfo_;

    TErrorOr<TPeerInfo> GetPeerInfo(TNodeId nodeId) noexcept
    {
        const auto* descriptor = NodeDirectory_->FindDescriptor(nodeId);
        if (!descriptor) {
            return TError(
                NNodeTrackerClient::EErrorCode::NoSuchNode,
                "Unresolved node id %v in node directory",
                nodeId);
        }

        auto address = descriptor->FindAddress(Networks_);
        if (!address) {
            return TError(
                NNodeTrackerClient::EErrorCode::NoSuchNetwork,
                "Cannot find any of %v addresses for seed %v",
                Networks_,
                descriptor->GetDefaultAddress());
        }

        TAddressWithNetwork addressWithNetwork;
        try {
            addressWithNetwork = descriptor->GetAddressWithNetworkOrThrow(Networks_);
        } catch (const std::exception& ex) {
            // TODO(akozhikhov): mark node as suspicious?
            return TError(ex);
        }

        const auto& channelFactory = Client_->GetChannelFactory();
        return TPeerInfo{
            .Address = addressWithNetwork,
            .Channel = channelFactory->CreateChannel(addressWithNetwork)
        };
    }

    void RunPeriodicUpdate();

    void SchedulePeriodicUpdate()
    {
        TDelayedExecutor::Submit(
            BIND([weakReader = MakeWeak(this)] {
                if (auto reader = weakReader.Lock()) {
                    reader->RunPeriodicUpdate();
                }
            })
            .Via(ReaderInvoker_),
            Config_->PeriodicUpdateDelay);
    }
};

DEFINE_REFCOUNTED_TYPE(TChunkFragmentReader)

////////////////////////////////////////////////////////////////////////////////

class TChunkFragmentReader::TSessionBase
    : public TRefCounted
{
public:
    TSessionBase(
        TChunkFragmentReaderPtr reader,
        TClientChunkReadOptions options)
        : Reader_(std::move(reader))
        , Options_(std::move(options))
        , Config_(Reader_->Config_)
        , SessionInvoker_(Reader_->ReaderInvoker_)
        , Logger(Reader_->Logger.WithTag("SessionId: %v, ReadSessionId: %v",
            TGuid::Create(),
            Options_.ReadSessionId))
    { }

protected:
    struct TPeerProbingInfo
    {
        TNodeId NodeId;
        TErrorOr<TPeerInfo> PeerInfoOrError;

        std::vector<int> ChunkIndexes;
        std::vector<TChunkId> ChunkIds;
    };

    const TChunkFragmentReaderPtr Reader_;
    const TClientChunkReadOptions Options_;
    const TChunkFragmentReaderConfigPtr Config_;
    const IInvokerPtr SessionInvoker_;

    const NLogging::TLogger Logger;

    NProfiling::TWallTimer Timer_;


    std::vector<TFuture<TChunkReplicaList>> InitializeAndGetChunkReplicaListFutures(int chunkCount)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        ChunkReplicaListFutures_.clear();
        ChunkIdToReplicaLocationInfo_.clear();

        ChunkReplicaListFutures_.reserve(chunkCount);
        ChunkIdToReplicaLocationInfo_.reserve(chunkCount);

        std::vector<TChunkId> chunkIds;
        chunkIds.reserve(chunkCount);
        for (int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
            chunkIds.push_back(GetPendingChunkId(chunkIndex));
        }

        auto locators = Reader_->ChunkReplicaLocatorCache_->Get(chunkIds);
        YT_VERIFY(locators.size() == chunkIds.size());
        for (int i = 0; i < std::ssize(locators); ++i) {
            auto&& locator = locators[i];
            ChunkReplicaListFutures_.push_back(locator->GetReplicas());
            YT_VERIFY(ChunkIdToReplicaLocationInfo_.emplace(
                chunkIds[i],
                TChunkReplicaLocationInfo{
                    .Locator = std::move(locator),
                    .FutureIndex = i
                })
                .second);
        }

        return ChunkReplicaListFutures_;
    }

    void TryDiscardChunkReplicas(TChunkId chunkId) const
    {
        auto it = ChunkIdToReplicaLocationInfo_.find(chunkId);
        if (it == ChunkIdToReplicaLocationInfo_.end()) {
            return;
        }

        it->second.Locator->DiscardReplicas(
            ChunkReplicaListFutures_[it->second.FutureIndex]);
    }

    std::vector<TPeerProbingInfo> GetProbingInfos(
        const std::vector<TChunkReplicaList>& chunkReplicaLists)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        THashMap<TNodeId, int> nodeIdToPeerIndex;
        std::vector<TNodeId> nodeIds;
        std::vector<TPeerProbingInfo> probingInfos;

        for (int chunkIndex = 0; chunkIndex < std::ssize(chunkReplicaLists); ++chunkIndex) {
            auto chunkId = GetPendingChunkId(chunkIndex);

            const auto& chunkReplicaList = chunkReplicaLists[chunkIndex];
            if (chunkReplicaList.empty()) {
                // NB: This branch is possible within periodic updates.
                continue;
            }

            for (auto chunkReplica : chunkReplicaList) {
                auto nodeId = chunkReplica.GetNodeId();
                auto [it, emplaced] = nodeIdToPeerIndex.try_emplace(nodeId);
                if (emplaced) {
                    it->second = probingInfos.size();
                    probingInfos.push_back({
                        .NodeId = nodeId
                    });
                    nodeIds.push_back(nodeId);
                }

                probingInfos[it->second].ChunkIndexes.push_back(chunkIndex);
                probingInfos[it->second].ChunkIds.push_back(chunkId);
            }
        }

        auto peerInfoOrErrors = Reader_->PeerInfoCache_->Get(nodeIds);
        for (int i = 0; i < std::ssize(peerInfoOrErrors); ++i) {
            YT_VERIFY(!peerInfoOrErrors[i].IsOK() || peerInfoOrErrors[i].Value().Channel);
            probingInfos[i].PeerInfoOrError = std::move(peerInfoOrErrors[i]);
        }

        return probingInfos;
    }

    TFuture<TProbeChunkSetResult> DoProbePeer(const TPeerProbingInfo& probingInfo) const
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        const auto& peerInfo = probingInfo.PeerInfoOrError.Value();
        YT_VERIFY(peerInfo.Channel);

        TDataNodeServiceProxy proxy(peerInfo.Channel);
        proxy.SetDefaultTimeout(Config_->ProbeChunkSetRpcTimeout);

        auto req = proxy.ProbeChunkSet();
        req->SetHeavy(true);
        ToProto(req->mutable_workload_descriptor(), Options_.WorkloadDescriptor);

        ToProto(req->mutable_chunk_ids(), probingInfo.ChunkIds);

        req->SetAcknowledgementTimeout(std::nullopt);

        return req->Invoke();
    }

    double ComputeProbingPenalty(i64 netQueueSize, i64 diskQueueSize) const
    {
        return
            Config_->NetQueueSizeFactor * netQueueSize +
            Config_->DiskQueueSizeFactor * diskQueueSize;
    }

    virtual TChunkId GetPendingChunkId(int chunkIndex) const = 0;

private:
    // NB: With this we can easily discard replicas whenever necessary.
    struct TChunkReplicaLocationInfo
    {
        TChunkReplicaLocatorPtr Locator;
        int FutureIndex;
    };

    std::vector<TFuture<TChunkReplicaList>> ChunkReplicaListFutures_;
    THashMap<TChunkId, TChunkReplicaLocationInfo> ChunkIdToReplicaLocationInfo_;
};

////////////////////////////////////////////////////////////////////////////////

class TChunkFragmentReader::TReadFragmentsSession
    : public TSessionBase
{
public:
    TReadFragmentsSession(
        TChunkFragmentReaderPtr reader,
        TClientChunkReadOptions options,
        std::vector<TChunkFragmentReader::TChunkFragmentRequest> requests)
        : TSessionBase(std::move(reader), std::move(options))
        , Requests_(std::move(requests))
        , Fragments_(Requests_.size())
    { }

    ~TReadFragmentsSession()
    {
        Promise_.TrySet(TError(NYT::EErrorCode::Canceled, "Read fragments session destroyed after %v",
            Timer_.GetElapsedTime()));
    }

    TFuture<std::vector<TSharedRef>> Run()
    {
        DoRun();

        return Promise_;
    }

private:
    struct TFragmentInfo
    {
        int FragmentIndex;
        i64 Offset;
        i64 Length;
    };

    struct TChunkFragmentSetInfo
    {
        TChunkId ChunkId;
        std::vector<TFragmentInfo> FragmentInfos;
    };

    struct TGetChunkFragmentSetInfo
    {
        TPeerInfo PeerInfo;
        std::vector<TChunkFragmentSetInfo> ChunkFragmentSetInfos;
    };

    const std::vector<TChunkFragmentReader::TChunkFragmentRequest> Requests_;
    const TPromise<std::vector<TSharedRef>> Promise_ = NewPromise<std::vector<TSharedRef>>();

    // Preprocessed chunk requests grouped by node.
    THashMap<TNodeId, TGetChunkFragmentSetInfo> PeerToRequestInfo_;
    // Chunk requests that are not assigned to any node yet.
    std::vector<TChunkFragmentSetInfo> PendingChunkFragmentSetInfos_;
    // Resulting fragments.
    std::vector<TSharedRef> Fragments_;

    int Iteration_ = 0;

    THashSet<TNodeId> BannedNodeIds_;
    std::vector<TError> InnerErrors_;


    virtual TChunkId GetPendingChunkId(int chunkIndex) const final
    {
        return PendingChunkFragmentSetInfos_[chunkIndex].ChunkId;
    }

    void DoRun()
    {
        if (Requests_.empty()) {
            Promise_.TrySet(Fragments_);
            return;
        }

        THashMap<TChunkId, std::vector<TFragmentInfo>> chunkIdToFragmentInfos;
        for (int i = 0; i < std::ssize(Requests_); ++i) {
            const auto& request = Requests_[i];

            chunkIdToFragmentInfos[request.ChunkId].push_back({
                .FragmentIndex = i,
                .Offset = request.Offset,
                .Length = request.Length
            });
        }

        {
            // TODO(akozhikhov): Check for suspicous nodes.

            auto readerGuard = ReaderGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            for (auto& [chunkId, fragmentInfos] : chunkIdToFragmentInfos) {
                auto peerInfoIt = Reader_->ChunkIdToPeerAccessInfo_.find(chunkId);
                if (peerInfoIt == Reader_->ChunkIdToPeerAccessInfo_.end()) {
                    PendingChunkFragmentSetInfos_.push_back({
                        .ChunkId = chunkId,
                        .FragmentInfos = std::move(fragmentInfos)
                    });
                    continue;
                }

                auto entryGuard = Guard(peerInfoIt->second.Lock);

                YT_VERIFY(peerInfoIt->second.Channel);

                auto [it, emplaced] = PeerToRequestInfo_.try_emplace(peerInfoIt->second.NodeId);
                if (emplaced) {
                    it->second.PeerInfo = peerInfoIt->second;
                }
                it->second.ChunkFragmentSetInfos.push_back({
                    .ChunkId = chunkId,
                    .FragmentInfos = std::move(fragmentInfos)
                });
            }
        }

        YT_LOG_DEBUG("Starting chunk fragment read session "
            "(TotalRequestCount: %v, TotalChunkCount: %v, PendingChunkCount: %v)",
            Requests_.size(),
            chunkIdToFragmentInfos.size(),
            PendingChunkFragmentSetInfos_.size());

        if (PendingChunkFragmentSetInfos_.empty()) {
            // Fast path.
            GetChunkFragments();
        } else {
            BIND(&TReadFragmentsSession::StartSessionIteration, MakeStrong(this))
                .Via(SessionInvoker_)
                .Run();
        }
    }

    void StartSessionIteration()
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(Iteration_ < Config_->MaxRetryCount);
        YT_VERIFY(!PendingChunkFragmentSetInfos_.empty());

        YT_LOG_DEBUG("Starting new iteration of chunk fragment read session "
            "(PendingChunkCount: %v, Iteration: %v)",
            PendingChunkFragmentSetInfos_.size(),
            Iteration_);

        auto futures = InitializeAndGetChunkReplicaListFutures(PendingChunkFragmentSetInfos_.size());

        auto future = AllSet(std::move(futures));
        if (auto result = future.TryGetUnique()) {
            ProbePeers(std::move(*result));
        } else {
            // TODO(akozhikhov): SubscribeUnique.
            future.Subscribe(BIND(
                &TReadFragmentsSession::ProbePeers,
                MakeStrong(this))
                .Via(SessionInvoker_));
        }
    }

    void ProbePeers(
        TErrorOr<std::vector<TErrorOr<TChunkReplicaList>>> resultOrError)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(resultOrError.IsOK());
        auto chunkReplicaListOrErrors = std::move(resultOrError.Value());

        YT_VERIFY(PendingChunkFragmentSetInfos_.size() == chunkReplicaListOrErrors.size());

        std::vector<TChunkReplicaList> chunkReplicaLists;
        chunkReplicaLists.reserve(PendingChunkFragmentSetInfos_.size());

        bool locateFailed = false;
        for (int i = 0; i < std::ssize(PendingChunkFragmentSetInfos_); ++i) {
            auto& chunkReplicaListOrError = chunkReplicaListOrErrors[i];
            if (!chunkReplicaListOrError.IsOK()) {
                auto error = TError(
                    NChunkClient::EErrorCode::MasterCommunicationFailed,
                    "Error requesting seeds from master for chunk %v",
                    PendingChunkFragmentSetInfos_[i].ChunkId)
                    << chunkReplicaListOrError;
                ProcessError(std::move(error));
                locateFailed = true;

                // NB: Even having periodic updates we still need to call discard here
                // because of chunks that are not in ChunkIdToPeerAccessInfo_ cache.
                TryDiscardChunkReplicas(PendingChunkFragmentSetInfos_[i].ChunkId);

                continue;
            }

            auto chunkReplicaList = std::move(chunkReplicaListOrError.Value());
            if (chunkReplicaList.empty()) {
                ProcessError(TError(
                    "Chunk %v is lost",
                    PendingChunkFragmentSetInfos_[i].ChunkId));
                locateFailed = true;

                TryDiscardChunkReplicas(PendingChunkFragmentSetInfos_[i].ChunkId);

                continue;
            }

            chunkReplicaLists.push_back(std::move(chunkReplicaList));
        }

        if (locateFailed) {
            ProcessFatalError(TError("Locate has failed"));
            return;
        }

        auto probingInfos = GetProbingInfos(chunkReplicaLists);

        std::vector<TPeerProbingInfo> goodProbingInfos;
        std::vector<TFuture<TProbeChunkSetResult>> probingFutures;

        goodProbingInfos.reserve(probingInfos.size());
        probingFutures.reserve(probingInfos.size());

        for (auto& probingInfo : probingInfos) {
            if (!probingInfo.PeerInfoOrError.IsOK()) {
                Reader_->PeerInfoCache_->Invalidate(probingInfo.NodeId);

                if (probingInfo.PeerInfoOrError.GetCode() == NNodeTrackerClient::EErrorCode::NoSuchNetwork) {
                    ProcessFatalError(probingInfo.PeerInfoOrError);
                    return;
                }

                ProcessError(probingInfo.PeerInfoOrError);
                BanPeer(probingInfo.NodeId, probingInfo.PeerInfoOrError);
                continue;
            }

            // TODO(akozhikhov): Fail with fatal error if all peers of a replica are banned.
            if (IsPeerBanned(probingInfo.NodeId)) {
                continue;
            }

            probingFutures.push_back(DoProbePeer(probingInfo));
            goodProbingInfos.push_back(std::move(probingInfo));
        }

        auto future = AllSet(std::move(probingFutures));
        if (const auto& result = future.TryGet()) {
            OnPeersProbed(
                std::move(goodProbingInfos),
                *result);
        } else {
            future.Subscribe(BIND(
                &TReadFragmentsSession::OnPeersProbed,
                MakeStrong(this),
                Passed(std::move(goodProbingInfos)))
                .Via(SessionInvoker_));
        }
    }

    void OnPeersProbed(
        std::vector<TPeerProbingInfo> probingInfos,
        const TErrorOr<std::vector<TErrorOrProbeChunkSetResult>>& resultOrError)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(resultOrError.IsOK());
        const auto& probingResultOrErrors = resultOrError.Value();

        YT_VERIFY(probingInfos.size() == probingResultOrErrors.size());

        std::vector<int> chunkBestNodeIndex(PendingChunkFragmentSetInfos_.size(), -1);
        std::vector<double> chunkLowestProbingPenalty(PendingChunkFragmentSetInfos_.size());

        for (int nodeIndex = 0; nodeIndex < std::ssize(probingInfos); ++nodeIndex) {
            const auto& probingInfo = probingInfos[nodeIndex];

            const auto& probingResultOrError = probingResultOrErrors[nodeIndex];
            if (!probingResultOrError.IsOK()) {
                YT_VERIFY(probingResultOrError.GetCode() != EErrorCode::NoSuchChunk);
                auto error = TError("Probing of peer %v has failed for %v chunks",
                    probingInfo.PeerInfoOrError.Value().Address,
                    probingInfo.ChunkIds.size())
                    << probingResultOrError;
                ProcessRpcError(std::move(error), probingInfo.NodeId, probingInfo.PeerInfoOrError);
                continue;
            }

            const auto& probingResult = probingResultOrError.Value();
            YT_VERIFY(std::ssize(probingInfo.ChunkIds) == probingResult->subresponses_size());

            if (probingResult->net_throttling()) {
                YT_LOG_DEBUG("Peer is throttling (Address: %v, NetQueueSize: %v)",
                    probingInfo.PeerInfoOrError.Value().Address,
                    probingResult->net_queue_size());
            }

            for (int resultIndex = 0; resultIndex < std::ssize(probingInfo.ChunkIds); ++resultIndex) {
                auto chunkIndex = probingInfo.ChunkIndexes[resultIndex];
                const auto& subresponse = probingResult->subresponses(resultIndex);

                if (!subresponse.has_complete_chunk()) {
                    // TODO(akozhikhov): consider suggested peer descriptors.
                    auto chunkId = probingInfo.ChunkIds[resultIndex];
                    YT_VERIFY(chunkId == PendingChunkFragmentSetInfos_[chunkIndex].ChunkId);
                    TryDiscardChunkReplicas(chunkId);

                    ProcessError(TError(
                        "Peer %v does not contain chunk %v",
                        probingInfo.PeerInfoOrError.Value().Address,
                        chunkId));
                    continue;
                }

                if (subresponse.disk_throttling()) {
                    YT_LOG_DEBUG("Peer is throttling (Address: %v, DiskQueueSize: %v)",
                        probingInfo.PeerInfoOrError.Value().Address,
                        subresponse.disk_queue_size());
                }

                auto currentProbingPenalty = ComputeProbingPenalty(
                    probingResult->net_queue_size(),
                    subresponse.disk_queue_size());
                if (chunkBestNodeIndex[chunkIndex] == -1 ||
                    currentProbingPenalty < chunkLowestProbingPenalty[chunkIndex])
                {
                    chunkBestNodeIndex[chunkIndex] = nodeIndex;
                    chunkLowestProbingPenalty[chunkIndex] = currentProbingPenalty;
                }
            }
        }

        for (int chunkIndex = 0; chunkIndex < std::ssize(chunkBestNodeIndex); ++chunkIndex) {
            auto& pendingChunkFragmentSetInfo = PendingChunkFragmentSetInfos_[chunkIndex];

            if (chunkBestNodeIndex[chunkIndex] == -1) {
                TryDiscardChunkReplicas(pendingChunkFragmentSetInfo.ChunkId);
                ProcessFatalError(TError(
                    "Peer probing has failed for chunk %v",
                    pendingChunkFragmentSetInfo.ChunkId));
                return;
            }

            const auto& probingInfo = probingInfos[chunkBestNodeIndex[chunkIndex]];
            YT_VERIFY(probingInfo.PeerInfoOrError.IsOK());

            auto [it, emplaced] = PeerToRequestInfo_.try_emplace(probingInfo.NodeId);
            if (emplaced) {
                it->second.PeerInfo = probingInfo.PeerInfoOrError.Value();
            }
            it->second.ChunkFragmentSetInfos.push_back(std::move(pendingChunkFragmentSetInfo));
        }

        YT_LOG_DEBUG("Finished probing peers (PeerCount: %v, ChunkCount: %v)",
            probingInfos.size(),
            PendingChunkFragmentSetInfos_.size());

        PendingChunkFragmentSetInfos_.clear();

        GetChunkFragments();
    }

    void GetChunkFragments()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        std::vector<TNodeId> nodeIds;
        std::vector<TFuture<TDataNodeServiceProxy::TRspGetChunkFragmentSetPtr>> responseFutures;
        nodeIds.reserve(PeerToRequestInfo_.size());
        responseFutures.reserve(PeerToRequestInfo_.size());

        for (const auto& [nodeId, requestInfo] : PeerToRequestInfo_) {
            if (requestInfo.ChunkFragmentSetInfos.empty()) {
                continue;
            }

            YT_VERIFY(requestInfo.PeerInfo.Channel);

            std::vector<TChunkId> chunkIds;
            chunkIds.reserve(requestInfo.ChunkFragmentSetInfos.size());

            TDataNodeServiceProxy proxy(requestInfo.PeerInfo.Channel);
            proxy.SetDefaultTimeout(Config_->GetChunkFragmentSetRpcTimeout);

            auto req = proxy.GetChunkFragmentSet();
            req->SetHeavy(true);
            req->SetMultiplexingBand(EMultiplexingBand::Heavy);

            ToProto(req->mutable_read_session_id(), Options_.ReadSessionId);
            ToProto(req->mutable_workload_descriptor(), Options_.WorkloadDescriptor);

            for (const auto& chunkFragmentSetInfos : requestInfo.ChunkFragmentSetInfos) {
                chunkIds.push_back(chunkFragmentSetInfos.ChunkId);

                auto* subrequest = req->add_subrequests();
                ToProto(subrequest->mutable_chunk_id(), chunkFragmentSetInfos.ChunkId);

                for (const auto& fragmentInfo : chunkFragmentSetInfos.FragmentInfos) {
                    auto* fragment = subrequest->add_fragments();
                    fragment->set_offset(fragmentInfo.Offset);
                    fragment->set_length(fragmentInfo.Length);
                }
            }

            req->Header().set_response_memory_zone(ToProto<int>(NYTAlloc::EMemoryZone::Undumpable));

            nodeIds.push_back(nodeId);
            responseFutures.push_back(req->Invoke());

            YT_LOG_DEBUG("Requesting chunk fragments (NodeId: %v, Address: %v, ChunkIds: %v)",
                nodeId,
                requestInfo.PeerInfo.Address,
                chunkIds);
        }

        AllSet(std::move(responseFutures)).Subscribe(BIND(
            &TReadFragmentsSession::OnGotChunkFragments,
            MakeStrong(this),
            Passed(std::move(nodeIds)))
            .Via(SessionInvoker_));
    }

    void OnGotChunkFragments(
        std::vector<TNodeId> nodeIds,
        const TErrorOr<std::vector<TDataNodeServiceProxy::TErrorOrRspGetChunkFragmentSetPtr>>& resultOrError)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(resultOrError.IsOK());
        const auto& responseOrErrors = resultOrError.Value();

        YT_VERIFY(nodeIds.size() == responseOrErrors.size());

        std::vector<std::vector<int>> failedChunkIndexesByNode;
        failedChunkIndexesByNode.reserve(nodeIds.size());

        for (int nodeIndex = 0; nodeIndex < std::ssize(nodeIds); ++nodeIndex) {
            auto nodeId = nodeIds[nodeIndex];
            const auto& responseOrError = responseOrErrors[nodeIndex];

            auto it = PeerToRequestInfo_.find(nodeId);
            YT_VERIFY(it != PeerToRequestInfo_.end());

            const auto& peerInfo = it->second.PeerInfo;
            auto& chunkFragmentSetInfos = it->second.ChunkFragmentSetInfos;
            auto& failedChunkIndexes = failedChunkIndexesByNode.emplace_back();

            if (!responseOrError.IsOK()) {
                auto error = TError("Failed to get chunk fragment set from peer %v",
                    peerInfo.Address)
                    << responseOrError;
                ProcessRpcError(std::move(error), nodeId, peerInfo);

                // TODO(akozhikhov): NoSuchChunk error should not be an issue after YT-14660.
                // YT_VERIFY(error.GetCode() != EErrorCode::NoSuchChunk);

                PendingChunkFragmentSetInfos_.reserve(
                    PendingChunkFragmentSetInfos_.size() + chunkFragmentSetInfos.size());
                std::move(
                    chunkFragmentSetInfos.begin(),
                    chunkFragmentSetInfos.end(),
                    std::back_inserter(PendingChunkFragmentSetInfos_));

                chunkFragmentSetInfos.clear();

                continue;
            }

            const auto& response = responseOrError.Value();

            if (response->has_chunk_reader_statistics()) {
                // TODO(akozhikhov): gather other statistics too.
                UpdateFromProto(&Options_.ChunkReaderStatistics, response->chunk_reader_statistics());
            }

            YT_VERIFY(response->subresponses_size() == std::ssize(chunkFragmentSetInfos));

            int attachmentIndex = 0;
            for (int i = 0; i < std::ssize(chunkFragmentSetInfos); ++i) {
                auto& chunkFragmentSetInfo = chunkFragmentSetInfos[i];

                const auto& subresponse = response->subresponses(i);
                if (!subresponse.has_complete_chunk()) {
                    // TODO(akozhikhov): Consider suggested peers.
                    TryDiscardChunkReplicas(chunkFragmentSetInfo.ChunkId);

                    auto error = TError("Peer %v does not contain chunk %v",
                        peerInfo.Address,
                        chunkFragmentSetInfo.ChunkId);
                    ProcessError(std::move(error));

                    attachmentIndex += chunkFragmentSetInfo.FragmentInfos.size();
                    PendingChunkFragmentSetInfos_.push_back(std::move(chunkFragmentSetInfo));

                    failedChunkIndexes.push_back(i);

                    continue;
                }

                // NB: If we have received any fragments of a chunk, then we have received all fragments of the chunk.
                for (const auto& fragmentInfo : chunkFragmentSetInfo.FragmentInfos) {
                    auto& fragment = response->Attachments()[attachmentIndex++];
                    YT_VERIFY(fragment);
                    Fragments_[fragmentInfo.FragmentIndex] = std::move(fragment);
                }
            }
            YT_VERIFY(attachmentIndex == std::ssize(response->Attachments()));
        }

        auto finished = PendingChunkFragmentSetInfos_.empty();
        if (finished) {
            Promise_.TrySet(std::move(Fragments_));
            YT_LOG_DEBUG("Chunk fragment read session finished successfully (Iteration: %v, WallTime: %v)",
                Iteration_,
                Timer_.GetElapsedTime());
        }

        auto now = NProfiling::GetInstant();
        int updateCount = 0;
        int reinsertCount = 0;
        std::vector<std::pair<TNodeId, TChunkId>> failedEntries;
        std::vector<std::pair<TChunkId, TPeerAccessInfo>> newEntries;
        {
            auto readerGuard = ReaderGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            for (int nodeIndex = 0; nodeIndex < std::ssize(nodeIds); ++nodeIndex) {
                auto nodeId = nodeIds[nodeIndex];
                const auto& failedChunkIndexes = failedChunkIndexesByNode[nodeIndex];
                const auto& responseOrError = responseOrErrors[nodeIndex];

                auto it = PeerToRequestInfo_.find(nodeId);
                YT_VERIFY(it != PeerToRequestInfo_.end());
                auto& requestInfo = it->second;
                auto& chunkFragmentSetInfos = requestInfo.ChunkFragmentSetInfos;

                if (!responseOrError.IsOK()) {
                    YT_VERIFY(chunkFragmentSetInfos.empty());
                    continue;
                }

                int failedChunkCount = 0;
                for (int chunkIndex = 0; chunkIndex < std::ssize(chunkFragmentSetInfos); ++chunkIndex) {
                    auto chunkId = chunkFragmentSetInfos[chunkIndex].ChunkId;
                    auto it = Reader_->ChunkIdToPeerAccessInfo_.find(chunkId);

                    if (failedChunkCount < std::ssize(failedChunkIndexes) &&
                        chunkIndex == failedChunkIndexes[failedChunkCount])
                    {
                        ++failedChunkCount;
                        failedEntries.emplace_back(nodeId, chunkId);

                        continue;
                    }

                    if (it == Reader_->ChunkIdToPeerAccessInfo_.end()) {
                        newEntries.emplace_back(
                            chunkId,
                            TPeerAccessInfo{
                                {requestInfo.PeerInfo},
                                .NodeId = nodeId,
                                .LastSuccessfulAccessTime = now
                            });
                        continue;
                    }

                    auto entryGuard = Guard(it->second.Lock);

                    it->second.LastSuccessfulAccessTime = now;
                    if (nodeId == it->second.NodeId) {
                        ++updateCount;
                    } else {
                        ++reinsertCount;
                        it->second.NodeId = nodeId;
                        it->second.Address = requestInfo.PeerInfo.Address;
                        it->second.Channel = requestInfo.PeerInfo.Channel;
                    }
                }
                YT_VERIFY(failedChunkCount == std::ssize(failedChunkIndexes));

                chunkFragmentSetInfos.clear();
            }
        }

        int eraseCount = 0;
        int insertCount = 0;
        if (!failedEntries.empty() || !newEntries.empty()) {
            auto guard = WriterGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            for (auto [nodeId, chunkId] : failedEntries) {
                auto it = Reader_->ChunkIdToPeerAccessInfo_.find(chunkId);
                if (it != Reader_->ChunkIdToPeerAccessInfo_.end() &&
                    it->second.NodeId == nodeId)
                {
                    Reader_->ChunkIdToPeerAccessInfo_.erase(it);
                    ++eraseCount;
                }
            }

            for (auto& [chunkId, peerAccessInfo] : newEntries) {
                auto emplaced = Reader_->ChunkIdToPeerAccessInfo_.emplace(
                    chunkId,
                    std::move(peerAccessInfo))
                    .second;
                if (emplaced) {
                    ++insertCount;
                }
            }
        }

        YT_LOG_DEBUG("Updated chunk successful access times "
            "(UpdateCount: %v, ReinsertCount: %v, "
            "InsertCount: %v, ExpectedInsertCount: %v, EraseCount: %v, ExpectedEraseCount: %v, "
            "WallTime: %v)",
            updateCount,
            reinsertCount,
            insertCount,
            newEntries.size(),
            eraseCount,
            failedEntries.size(),
            NProfiling::GetInstant() - now);

        if (finished) {
            return;
        }

        if (++Iteration_ >= Config_->MaxRetryCount) {
            ProcessFatalError(TError(
                "Retry count limit %v was exceeded",
                Config_->MaxRetryCount));
            return;
        }

        TDelayedExecutor::Submit(
            BIND(&TReadFragmentsSession::StartSessionIteration, MakeStrong(this))
                .Via(SessionInvoker_),
            Config_->RetryBackoffTime);
    }

    void ProcessError(TError error)
    {
        YT_LOG_ERROR(error);
        InnerErrors_.push_back(std::move(error));
    }

    void ProcessRpcError(TError error, TNodeId nodeId, const TErrorOr<TPeerInfo>& peerInfo)
    {
        // TODO(akozhikhov): Check node for suspiciousness.

        ProcessError(std::move(error));

        BanPeer(nodeId, peerInfo);
    }

    void ProcessFatalError(TError error) const
    {
        YT_LOG_ERROR(error);
        Promise_.TrySet(error << InnerErrors_);
    }

    void BanPeer(TNodeId nodeId, const TErrorOr<TPeerInfo>& peerInfoOrError)
    {
        auto address = peerInfoOrError.IsOK()
            ? peerInfoOrError.Value().Address
            : TAddressWithNetwork();
        YT_LOG_DEBUG("Node is banned for the current session (NodeId: %v, Address: %v)",
            nodeId,
            address);

        BannedNodeIds_.insert(nodeId);
    }

    bool IsPeerBanned(TNodeId nodeId)
    {
        return BannedNodeIds_.contains(nodeId);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TChunkFragmentReader::TPeriodicUpdateSession
    : public TSessionBase
{
public:
    explicit TPeriodicUpdateSession(
        TChunkFragmentReaderPtr reader)
        : TSessionBase(std::move(reader), MakeOptions())
    { }

    void Run()
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_LOG_DEBUG("Starting chunk fragment reader periodic update session");

        FindFreshAndObsoleteChunks();

        if (ChunkIds_.empty()) {
            EraseBadChunksIfAny();
            YT_LOG_DEBUG("Chunk fragment reader periodic update session finished due to empty chunk set");
            Reader_->SchedulePeriodicUpdate();
            return;
        }

        auto futures = InitializeAndGetChunkReplicaListFutures(ChunkIds_.size());

        AllSet(std::move(futures)).Subscribe(BIND(
            &TPeriodicUpdateSession::OnReplicasLocated,
            MakeStrong(this))
            .Via(SessionInvoker_));
    }

private:
    std::vector<TChunkId> ChunkIds_;
    std::vector<TChunkReplicaList> ChunkReplicaLists_;

    std::vector<TChunkId> ObsoleteChunkIds_;
    std::vector<TChunkId> MissingChunkIds_;
    std::vector<TChunkId> NonexistentChunkIds_;
    std::vector<TChunkId> FailedChunkIds_;

    static TClientChunkReadOptions MakeOptions()
    {
        return {
            // TODO(akozhikhov): Employ some system category.
            .WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::UserBatch),
            .ReadSessionId = TReadSessionId::Create()
        };
    }

    bool IsChunkObsolete(const TPeerAccessInfo& peerAccessInfo, TInstant now) const
    {
        return peerAccessInfo.LastSuccessfulAccessTime + Config_->EvictAfterSuccessfulAccessTime < now;
    }

    void FindFreshAndObsoleteChunks()
    {
        auto now = NProfiling::GetInstant();

        {
            auto readerGuard = ReaderGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            for (const auto& [chunkId, peerAccessInfo] : Reader_->ChunkIdToPeerAccessInfo_) {
                auto entryGuard = Guard(peerAccessInfo.Lock);

                YT_VERIFY(peerAccessInfo.LastSuccessfulAccessTime);
                if (IsChunkObsolete(peerAccessInfo, now)) {
                    ObsoleteChunkIds_.push_back(chunkId);
                } else {
                    ChunkIds_.push_back(chunkId);
                }
            }
        }
    }

    void OnReplicasLocated(
        TErrorOr<std::vector<TErrorOr<TChunkReplicaList>>> resultOrError)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(resultOrError.IsOK());
        auto chunkReplicaListOrErrors = std::move(resultOrError.Value());

        YT_VERIFY(ChunkIds_.size() == chunkReplicaListOrErrors.size());

        ChunkReplicaLists_.reserve(ChunkIds_.size());

        for (int i = 0; i < std::ssize(ChunkIds_); ++i) {
            auto& chunkReplicaListOrError = chunkReplicaListOrErrors[i];
            if (!chunkReplicaListOrError.IsOK()) {
                if (chunkReplicaListOrError.GetCode() == EErrorCode::NoSuchChunk) {
                    NonexistentChunkIds_.push_back(ChunkIds_[i]);
                }
                TryDiscardChunkReplicas(ChunkIds_[i]);
                ChunkReplicaLists_.emplace_back();
                continue;
            }

            ChunkReplicaLists_.push_back(std::move(chunkReplicaListOrError.Value()));
            if (ChunkReplicaLists_.back().empty()) {
                MissingChunkIds_.push_back(ChunkIds_[i]);
                TryDiscardChunkReplicas(ChunkIds_[i]);
            }
        }

        auto probingInfos = GetProbingInfos(ChunkReplicaLists_);

        std::vector<TFuture<TProbeChunkSetResult>> probingFutures;
        probingFutures.reserve(probingInfos.size());
        for (const auto& probingInfo : probingInfos) {
            if (!probingInfo.PeerInfoOrError.IsOK()) {
                YT_LOG_ERROR(probingInfo.PeerInfoOrError, "Failed to obtain peer info");

                // TODO(akozhikhov): Node should be marked suspicious as we cannot get address.
                Reader_->PeerInfoCache_->Invalidate(probingInfo.NodeId);

                probingFutures.push_back(MakeFuture<TProbeChunkSetResult>({}));
                continue;
            }

            probingFutures.push_back(DoProbePeer(probingInfo));
        }

        AllSet(std::move(probingFutures)).Subscribe(BIND(
            &TPeriodicUpdateSession::OnPeersProbed,
            MakeStrong(this),
            Passed(std::move(probingInfos)))
            .Via(SessionInvoker_));
    }

    void OnPeersProbed(
        std::vector<TPeerProbingInfo> probingInfos,
        const TErrorOr<std::vector<TErrorOrProbeChunkSetResult>>& resultOrError)
    {
        VERIFY_INVOKER_AFFINITY(SessionInvoker_);

        YT_VERIFY(resultOrError.IsOK());
        const auto& probingResultOrErrors = resultOrError.Value();

        YT_VERIFY(probingInfos.size() == probingResultOrErrors.size());

        std::vector<int> chunkBestNodeIndex(ChunkIds_.size(), -1);
        std::vector<double> chunkLowestProbingPenalty(ChunkIds_.size());

        for (int nodeIndex = 0; nodeIndex < std::ssize(probingInfos); ++nodeIndex) {
            const auto& probingInfo = probingInfos[nodeIndex];
            if (!probingInfo.PeerInfoOrError.IsOK()) {
                continue;
            }

            const auto& probingResultOrError = probingResultOrErrors[nodeIndex];

            if (!probingResultOrError.IsOK()) {
                YT_LOG_ERROR(probingResultOrError, "Failed to probe peer (Address: %v)",
                    probingInfo.PeerInfoOrError.Value().Address);

                YT_VERIFY(probingResultOrError.GetCode() != EErrorCode::NoSuchChunk);
                // TODO(akozhikhov): Mark node as suspicious.
                // TODO(akozhikhov): Don't invalidate upon specific errors like RequestQueueSizeLimitExceeded.
                Reader_->PeerInfoCache_->Invalidate(probingInfo.NodeId);
                continue;
            }

            const auto& probingResult = probingResultOrError.Value();
            YT_VERIFY(std::ssize(probingInfo.ChunkIndexes) == probingResult->subresponses_size());

            for (int resultIndex = 0; resultIndex < std::ssize(probingInfo.ChunkIndexes); ++resultIndex) {
                auto chunkIndex = probingInfo.ChunkIndexes[resultIndex];
                const auto& subresponse = probingResult->subresponses(resultIndex);

                if (!subresponse.has_complete_chunk()) {
                    auto chunkId = probingInfo.ChunkIds[resultIndex];
                    YT_VERIFY(chunkId == ChunkIds_[chunkIndex]);
                    YT_LOG_ERROR("Chunk is missing from node (ChunkId: %v, Address: %v)",
                        chunkId,
                        probingInfo.PeerInfoOrError.Value().Address);

                    // TODO(akozhikhov): Consider suggested peer descriptors.
                    TryDiscardChunkReplicas(chunkId);
                    continue;
                }

                auto currentProbingPenalty = ComputeProbingPenalty(
                    probingResult->net_queue_size(),
                    subresponse.disk_queue_size());
                if (chunkBestNodeIndex[chunkIndex] == -1 ||
                    currentProbingPenalty < chunkLowestProbingPenalty[chunkIndex])
                {
                    chunkBestNodeIndex[chunkIndex] = nodeIndex;
                    chunkLowestProbingPenalty[chunkIndex] = currentProbingPenalty;
                }
            }
        }

        {
            auto readerGuard = ReaderGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            for (int chunkIndex = 0; chunkIndex < std::ssize(chunkBestNodeIndex); ++chunkIndex) {
                auto chunkId = ChunkIds_[chunkIndex];
                if (chunkBestNodeIndex[chunkIndex] == -1) {
                    YT_LOG_ERROR("Peer probing failed for chunk (ChunkId: %v)",
                        chunkId);

                    TryDiscardChunkReplicas(chunkId);
                    FailedChunkIds_.push_back(chunkId);
                    continue;
                }

                auto it = Reader_->ChunkIdToPeerAccessInfo_.find(chunkId);
                if (it != Reader_->ChunkIdToPeerAccessInfo_.end()) {
                    auto entryGuard = Guard(it->second.Lock);

                    const auto& probingInfo = probingInfos[chunkBestNodeIndex[chunkIndex]];
                    if (it->second.NodeId != probingInfo.NodeId) {
                        it->second.NodeId = probingInfo.NodeId;

                        const auto& peerInfo = probingInfo.PeerInfoOrError.Value();
                        it->second.Address = peerInfo.Address;
                        it->second.Channel = peerInfo.Channel;
                    }
                }
            }
        }

        EraseBadChunksIfAny();

        YT_LOG_DEBUG("Chunk fragment reader periodic update session successfully finished (WallTime: %v)",
            Timer_.GetElapsedTime());

        Reader_->SchedulePeriodicUpdate();
    }

    void EraseBadChunksIfAny()
    {
        if (!ObsoleteChunkIds_.empty() ||
            !MissingChunkIds_.empty() ||
            !NonexistentChunkIds_.empty() ||
            !FailedChunkIds_.empty())
        {
            auto guard = WriterGuard(Reader_->ChunkIdToPeerAccessInfoLock_);

            auto now = NProfiling::GetInstant();

            for (auto chunkId : ObsoleteChunkIds_) {
                auto it = Reader_->ChunkIdToPeerAccessInfo_.find(chunkId);
                if (it != Reader_->ChunkIdToPeerAccessInfo_.end() &&
                    IsChunkObsolete(it->second, now))
                {
                    Reader_->ChunkIdToPeerAccessInfo_.erase(it);
                }
            }

            for (auto chunkId : MissingChunkIds_) {
                Reader_->ChunkIdToPeerAccessInfo_.erase(chunkId);
            }

            for (auto chunkId : NonexistentChunkIds_) {
                Reader_->ChunkIdToPeerAccessInfo_.erase(chunkId);
            }

            for (auto chunkId : FailedChunkIds_) {
                Reader_->ChunkIdToPeerAccessInfo_.erase(chunkId);
            }
        }

        YT_LOG_DEBUG("Erased bad chunks within periodic update session "
            "(ObsoleteChunkCount: %v, MissingChunkCount: %v, NonexistentChunkCount: %v, FailedChunkCount: %v)",
            ObsoleteChunkIds_.size(),
            MissingChunkIds_.size(),
            NonexistentChunkIds_.size(),
            FailedChunkIds_.size());
    }

    virtual TChunkId GetPendingChunkId(int chunkIndex) const final
    {
        return ChunkIds_[chunkIndex];
    }
};

////////////////////////////////////////////////////////////////////////////////

TFuture<std::vector<TSharedRef>> TChunkFragmentReader::ReadFragments(
    TClientChunkReadOptions options,
    std::vector<TChunkFragmentRequest> requests)
{
    auto session = New<TReadFragmentsSession>(
        this,
        std::move(options),
        std::move(requests));
    return session->Run();
}

void TChunkFragmentReader::RunPeriodicUpdate()
{
    auto session = New<TPeriodicUpdateSession>(this);
    session->Run();
}

////////////////////////////////////////////////////////////////////////////////

IChunkFragmentReaderPtr CreateChunkFragmentReader(
    TChunkFragmentReaderConfigPtr config,
    IClientPtr client,
    INodeStatusDirectoryPtr nodeStatusDirectory)
{
    return New<TChunkFragmentReader>(
        std::move(config),
        std::move(client),
        std::move(nodeStatusDirectory));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
