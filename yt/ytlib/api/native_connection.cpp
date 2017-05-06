#include "admin.h"
#include "config.h"
#include "native_connection.h"
#include "native_client.h"
#include "native_admin.h"
#include "native_transaction_participant.h"
#include "native_transaction.h"
#include "private.h"

#include <yt/ytlib/chunk_client/client_block_cache.h>

#include <yt/ytlib/hive/cell_directory.h>
#include <yt/ytlib/hive/cluster_directory.h>
#include <yt/ytlib/hive/cluster_directory_synchronizer.h>

#include <yt/ytlib/hydra/peer_channel.h>

#include <yt/ytlib/query_client/column_evaluator.h>
#include <yt/ytlib/query_client/evaluator.h>
#include <yt/ytlib/query_client/functions_cache.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/scheduler/scheduler_channel.h>

#include <yt/ytlib/tablet_client/table_mount_cache.h>
#include <yt/ytlib/tablet_client/native_table_mount_cache.h>

#include <yt/ytlib/transaction_client/config.h>
#include <yt/ytlib/transaction_client/remote_timestamp_provider.h>

#include <yt/ytlib/object_client/helpers.h>

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/thread_pool.h>
#include <yt/core/concurrency/lease_manager.h>

#include <yt/core/rpc/bus_channel.h>
#include <yt/core/rpc/caching_channel_factory.h>
#include <yt/core/rpc/retrying_channel.h>

namespace NYT {
namespace NApi {

using namespace NConcurrency;
using namespace NRpc;
using namespace NHiveClient;
using namespace NChunkClient;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NObjectClient;
using namespace NQueryClient;
using namespace NHydra;
using namespace NNodeTrackerClient;
using namespace NScheduler;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = ApiLogger;

////////////////////////////////////////////////////////////////////////////////

class TNativeConnection
    : public INativeConnection
{
public:
    TNativeConnection(
        TNativeConnectionConfigPtr config,
        const TNativeConnectionOptions& options)
        : Config_(config)
        , Options_(options)
    { }

    void Initialize()
    {
        LightPool_ = New<TThreadPool>(Config_->LightPoolSize, "ClientLight");
        HeavyPool_ = New<TThreadPool>(Config_->HeavyPoolSize, "ClientHeavy");

        PrimaryMasterCellId_ = Config_->PrimaryMaster->CellId;
        PrimaryMasterCellTag_ = CellTagFromId(PrimaryMasterCellId_);
        for (const auto& masterConfig : Config_->SecondaryMasters) {
            SecondaryMasterCellTags_.push_back(CellTagFromId(masterConfig->CellId));
        }

        auto initMasterChannel = [&] (
            EMasterChannelKind channelKind,
            const TMasterConnectionConfigPtr& config,
            EPeerKind peerKind)
        {
            auto cellTag = CellTagFromId(config->CellId);
            MasterChannels_[channelKind][cellTag] = CreatePeerChannel(config, peerKind);
        };

        auto leaderPeerKind = EPeerKind::Leader;
        auto followerPeerKind = Config_->EnableReadFromFollowers ? EPeerKind::Follower : EPeerKind::Leader;

        auto initMasterChannels = [&] (const TMasterConnectionConfigPtr& config) {
            initMasterChannel(EMasterChannelKind::Leader, config, leaderPeerKind);
            initMasterChannel(EMasterChannelKind::Follower, config, followerPeerKind);
        };

        initMasterChannels(Config_->PrimaryMaster);
        for (const auto& masterConfig : Config_->SecondaryMasters) {
            initMasterChannels(masterConfig);
        }

        // NB: Caching is only possible for the primary master.
        auto masterCacheConfig = Config_->MasterCache ? Config_->MasterCache : Config_->PrimaryMaster;
        initMasterChannel(EMasterChannelKind::Cache, masterCacheConfig, followerPeerKind);

        auto timestampProviderConfig = Config_->TimestampProvider;
        if (!timestampProviderConfig) {
            // Use masters for timestamp generation.
            timestampProviderConfig = New<TRemoteTimestampProviderConfig>();
            timestampProviderConfig->Addresses = Config_->PrimaryMaster->Addresses;
            timestampProviderConfig->RpcTimeout = Config_->PrimaryMaster->RpcTimeout;
        }
        TimestampProvider_ = CreateRemoteTimestampProvider(
            timestampProviderConfig,
            LightChannelFactory_);

        SchedulerChannel_ = CreateSchedulerChannel(
            Config_->Scheduler,
            LightChannelFactory_,
            GetMasterChannelOrThrow(EMasterChannelKind::Leader),
            GetNetworks());

        CellDirectory_ = New<TCellDirectory>(
            Config_->CellDirectory,
            LightChannelFactory_,
            GetNetworks());
        CellDirectory_->ReconfigureCell(Config_->PrimaryMaster);
        for (const auto& cellConfig : Config_->SecondaryMasters) {
            CellDirectory_->ReconfigureCell(cellConfig);
        }

        BlockCache_ = CreateClientBlockCache(
            Config_->BlockCache,
            EBlockType::CompressedData|EBlockType::UncompressedData);

        TableMountCache_ = CreateNativeTableMountCache(
            Config_->TableMountCache,
            GetMasterChannelOrThrow(EMasterChannelKind::Cache),
            CellDirectory_);

        QueryEvaluator_ = New<TEvaluator>(Config_->QueryEvaluator);
        ColumnEvaluatorCache_ = New<TColumnEvaluatorCache>(Config_->ColumnEvaluatorCache);
    }

    // IConnection implementation.

    virtual TCellTag GetCellTag() override
    {
        return CellTagFromId(Config_->PrimaryMaster->CellId);
    }

    virtual const ITableMountCachePtr& GetTableMountCache() override
    {
        return TableMountCache_;
    }

    virtual const ITimestampProviderPtr& GetTimestampProvider() override
    {
        return TimestampProvider_;
    }

    virtual const IInvokerPtr& GetLightInvoker() override
    {
        return LightPool_->GetInvoker();
    }

    virtual const IInvokerPtr& GetHeavyInvoker() override
    {
        return HeavyPool_->GetInvoker();
    }

    virtual IAdminPtr CreateAdmin(const TAdminOptions& options) override
    {
        return CreateNativeAdmin(this, options);
    }

    virtual IClientPtr CreateClient(const TClientOptions& options) override
    {
        return CreateNativeClient(options);
    }

    virtual void ClearMetadataCaches() override
    {
        TableMountCache_->Clear();
    }

    // INativeConnection implementation.

    virtual const TNativeConnectionConfigPtr& GetConfig() override
    {
        return Config_;
    }

    virtual const TNetworkPreferenceList& GetNetworks() const override
    {
        return Config_->Networks.Get(DefaultNetworkPreferences);
    }

    virtual const TCellId& GetPrimaryMasterCellId() const override
    {
        return PrimaryMasterCellId_;
    }

    virtual TCellTag GetPrimaryMasterCellTag() const override
    {
        return PrimaryMasterCellTag_;
    }

    virtual const TCellTagList& GetSecondaryMasterCellTags() const override
    {
        return SecondaryMasterCellTags_;
    }

    virtual IChannelPtr GetMasterChannelOrThrow(
        EMasterChannelKind kind,
        TCellTag cellTag = PrimaryMasterCellTag) override
    {
        const auto& channels = MasterChannels_[kind];
        auto it = channels.find(cellTag == PrimaryMasterCellTag ? PrimaryMasterCellTag_ : cellTag);
        if (it == channels.end()) {
            THROW_ERROR_EXCEPTION("Unknown master cell tag %v",
                cellTag);
        }
        return it->second;
    }

    virtual const IChannelPtr& GetSchedulerChannel() override
    {
        return SchedulerChannel_;
    }

    virtual const IChannelFactoryPtr& GetLightChannelFactory() override
    {
        return LightChannelFactory_;
    }

    virtual const IChannelFactoryPtr& GetHeavyChannelFactory() override
    {
        return HeavyChannelFactory_;
    }

    virtual const IBlockCachePtr& GetBlockCache() override
    {
        return BlockCache_;
    }

    virtual const TCellDirectoryPtr& GetCellDirectory() override
    {
        return CellDirectory_;
    }

    virtual const TEvaluatorPtr& GetQueryEvaluator() override
    {
        return QueryEvaluator_;
    }

    virtual const TColumnEvaluatorCachePtr& GetColumnEvaluatorCache() override
    {
        return ColumnEvaluatorCache_;
    }


    virtual const TClusterDirectoryPtr& GetClusterDirectory() override
    {
        EnsureClusterDirectory();
        return ClusterDirectory_;
    }

    virtual TFuture<void> SyncClusterDirectory() override
    {
        EnsureClusterDirectory();
        return ClusterDirectorySynchronizer_->Sync();
    }


    virtual INativeClientPtr CreateNativeClient(const TClientOptions& options) override
    {
        return NApi::CreateNativeClient(this, options);
    }

    virtual NHiveClient::ITransactionParticipantPtr CreateTransactionParticipant(
        const TCellId& cellId,
        const TTransactionParticipantOptions& options) override
    {
        return NApi::CreateNativeTransactionParticipant(
            CellDirectory_,
            TimestampProvider_,
            cellId,
            options);
    }

    virtual INativeTransactionPtr RegisterStickyTransaction(INativeTransactionPtr transaction) override
    {
        const auto& transactionId = transaction->GetId();
        TStickyTransactionEntry entry{
            transaction,
            TLeaseManager::CreateLease(
                transaction->GetTimeout(),
                BIND(&TNativeConnection::OnStickyTransactionLeaseExpired, MakeWeak(this), transactionId))
        };

        {
            TWriterGuard guard(StickyTransactionLock_);
            YCHECK(IdToStickyTransactionEntry_.emplace(transactionId, entry).second);
        }

        transaction->SubscribeCommitted(BIND(&TNativeConnection::OnStickyTransactionFinished, MakeWeak(this), transactionId));
        transaction->SubscribeAborted(BIND(&TNativeConnection::OnStickyTransactionFinished, MakeWeak(this), transactionId));

        LOG_DEBUG("Sticky transaction registered (TransactionId: %v)",
            transactionId);

        return transaction;
    }

    virtual INativeTransactionPtr GetStickyTransaction(const TTransactionId& transactionId) override
    {
        INativeTransactionPtr transaction;
        TLease lease;
        {
            TReaderGuard guard(StickyTransactionLock_);
            auto it = IdToStickyTransactionEntry_.find(transactionId);
            if (it == IdToStickyTransactionEntry_.end()) {
                THROW_ERROR_EXCEPTION(
                    NTransactionClient::EErrorCode::NoSuchTransaction,
                    "Sticky transaction %v is not found",
                    transactionId);
            }
            const auto& entry = it->second;
            transaction = entry.Transaction;
            lease = entry.Lease;
        }
        TLeaseManager::RenewLease(lease);
        LOG_DEBUG("Sticky transaction lease renewed (TransactionId: %v)",
            transactionId);
        return transaction;
    }

    virtual void Terminate() override
    {
        LightPool_.Reset();
        HeavyPool_.Reset();
    }

private:
    const TNativeConnectionConfigPtr Config_;
    const TNativeConnectionOptions Options_;

    const NRpc::IChannelFactoryPtr LightChannelFactory_ = CreateCachingChannelFactory(GetBusChannelFactory());
    const NRpc::IChannelFactoryPtr HeavyChannelFactory_ = CreateCachingChannelFactory(GetBusChannelFactory());

    TCellId PrimaryMasterCellId_;
    TCellTag PrimaryMasterCellTag_;
    TCellTagList SecondaryMasterCellTags_;

    TEnumIndexedVector<yhash<TCellTag, IChannelPtr>, EMasterChannelKind> MasterChannels_;
    IChannelPtr SchedulerChannel_;
    IBlockCachePtr BlockCache_;
    ITableMountCachePtr TableMountCache_;
    ITimestampProviderPtr TimestampProvider_;
    TCellDirectoryPtr CellDirectory_;
    TEvaluatorPtr QueryEvaluator_;
    TColumnEvaluatorCachePtr ColumnEvaluatorCache_;

    TSpinLock ClusterDirectoryLock_;
    TClusterDirectoryPtr ClusterDirectory_;
    TClusterDirectorySynchronizerPtr ClusterDirectorySynchronizer_;

    TThreadPoolPtr LightPool_;
    TThreadPoolPtr HeavyPool_;

    struct TStickyTransactionEntry
    {
        INativeTransactionPtr Transaction;
        TLease Lease;
    };

    TReaderWriterSpinLock StickyTransactionLock_;
    yhash<TTransactionId, TStickyTransactionEntry> IdToStickyTransactionEntry_;


    void EnsureClusterDirectory()
    {
        // ClusterDirectory_ and ClusterDirectorySynchronizer_ are lazy-created.
        auto guard = Guard(ClusterDirectoryLock_);
        if (!ClusterDirectory_) {
            ClusterDirectory_ = New<TClusterDirectory>();
            ClusterDirectorySynchronizer_ = New<TClusterDirectorySynchronizer>(
                Config_->ClusterDirectorySynchronizer,
                this,
                ClusterDirectory_);
        }
    }

    IChannelPtr CreatePeerChannel(TMasterConnectionConfigPtr config, EPeerKind kind)
    {
        auto channel = NHydra::CreatePeerChannel(
            config,
            LightChannelFactory_,
            kind);

        auto isRetryableError = BIND([options = Options_] (const TError& error) {
            if (error.FindMatching(NChunkClient::EErrorCode::OptimisticLockFailure)) {
                return true;
            }

            if (options.RetryRequestQueueSizeLimitExceeded &&
                error.GetCode() == NSecurityClient::EErrorCode::RequestQueueSizeLimitExceeded)
            {
                return true;
            }

            return IsRetriableError(error);
        });
        channel = CreateRetryingChannel(config, channel, isRetryableError);

        channel = CreateDefaultTimeoutChannel(channel, config->RpcTimeout);

        return channel;
    }


    void OnStickyTransactionLeaseExpired(const TTransactionId& transactionId)
    {
        INativeTransactionPtr transaction;
        {
            TWriterGuard guard(StickyTransactionLock_);
            auto it = IdToStickyTransactionEntry_.find(transactionId);
            if (it == IdToStickyTransactionEntry_.end()) {
                return;
            }
            transaction = it->second.Transaction;
            IdToStickyTransactionEntry_.erase(it);
        }

        LOG_DEBUG("Sticky transaction lease expired (TransactionId: %v)",
            transactionId);

        transaction->Abort();
    }

    void OnStickyTransactionFinished(const TTransactionId& transactionId)
    {
        TLease lease;
        {
            TWriterGuard guard(StickyTransactionLock_);
            auto it = IdToStickyTransactionEntry_.find(transactionId);
            if (it == IdToStickyTransactionEntry_.end()) {
                return;
            }
            lease = it->second.Lease;
            IdToStickyTransactionEntry_.erase(it);
        }

        LOG_DEBUG("Sticky transaction unregistered (TransactionId: %v)",
            transactionId);

        TLeaseManager::CloseLease(lease);
    }
};

INativeConnectionPtr CreateNativeConnection(
    TNativeConnectionConfigPtr config,
    const TNativeConnectionOptions& options)
{
    auto connection = New<TNativeConnection>(config, options);
    connection->Initialize();
    return connection;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi
} // namespace NYT
