#include "chunk.h"

#include "chunk_list.h"
#include "chunk_tree_statistics.h"
#include "helpers.h"
#include "medium.h"
#include "private.h"

#include <yt/yt/server/master/cell_master/serialize.h>
#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/server/master/chunk_server/chunk_manager.h>

#include <yt/yt/server/master/node_tracker_server/node.h>

#include <yt/yt/server/master/security_server/account.h>

#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>

#include <yt/yt/ytlib/journal_client/helpers.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/library/erasure/impl/codec.h>

#include <yt/yt/core/misc/collection_helpers.h>

namespace NYT::NChunkServer {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NJournalClient;
using namespace NObjectServer;
using namespace NObjectClient;
using namespace NSecurityServer;
using namespace NCellMaster;
using namespace NJournalClient;

using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = ChunkServerLogger;

const TChunk::TCachedReplicas TChunk::EmptyCachedReplicas;
const TChunk::TEmptyChunkReplicasData TChunk::EmptyChunkReplicasData = {};

////////////////////////////////////////////////////////////////////////////////

TChunk::TChunk(TChunkId id)
    : TChunkTree(id)
    , ChunkMeta_(TImmutableChunkMeta::CreateNull())
    , ShardIndex_(GetChunkShardIndex(id))
    , AggregatedRequisitionIndex_(IsErasure()
        ? MigrationErasureChunkRequisitionIndex
        : MigrationChunkRequisitionIndex)
    , LocalRequisitionIndex_(AggregatedRequisitionIndex_)
{ }

TChunkTreeStatistics TChunk::GetStatistics() const
{
    TChunkTreeStatistics result;
    if (IsSealed()) {
        result.RowCount = GetRowCount();
        result.LogicalRowCount = GetRowCount();
        result.UncompressedDataSize = GetUncompressedDataSize();
        result.CompressedDataSize = GetCompressedDataSize();
        result.DataWeight = GetDataWeight();
        result.LogicalDataWeight = GetDataWeight();
        if (IsErasure()) {
            result.ErasureDiskSpace = GetDiskSpace();
        } else {
            result.RegularDiskSpace = GetDiskSpace();
        }
        result.ChunkCount = 1;
        result.LogicalChunkCount = 1;
        result.Rank = 0;
    }
    return result;
}

i64 TChunk::GetPartDiskSpace() const
{
    auto result = GetDiskSpace();
    auto codecId = GetErasureCodec();
    if (codecId != NErasure::ECodec::None) {
        auto* codec = NErasure::GetCodec(codecId);
        result /= codec->GetTotalPartCount();
    }

    return result;
}

TString TChunk::GetLowercaseObjectName() const
{
    switch (GetType()) {
        case EObjectType::Chunk:
            return Format("chunk %v", GetId());
        case EObjectType::ErasureChunk:
            return Format("erasure chunk %v", GetId());
        case EObjectType::JournalChunk:
            return Format("journal chunk %v", GetId());
        case EObjectType::ErasureJournalChunk:
            return Format("erasure journal chunk %v", GetId());
        default:
            YT_ABORT();
    }
}

TString TChunk::GetCapitalizedObjectName() const
{
    switch (GetType()) {
        case EObjectType::Chunk:
            return Format("Chunk %v", GetId());
        case EObjectType::ErasureChunk:
            return Format("Erasure chunk %v", GetId());
        case EObjectType::JournalChunk:
            return Format("Journal chunk %v", GetId());
        case EObjectType::ErasureJournalChunk:
            return Format("Erasure journal chunk %v", GetId());
        default:
            YT_ABORT();
    }
}

void TChunk::Save(NCellMaster::TSaveContext& context) const
{
    TChunkTree::Save(context);

    using NYT::Save;
    Save(context, ChunkMeta_);
    Save(context, AggregatedRequisitionIndex_);
    Save(context, LocalRequisitionIndex_);
    Save(context, GetReadQuorum());
    Save(context, GetWriteQuorum());
    Save(context, LogReplicaLagLimit_);
    Save(context, GetDiskSpace());
    Save(context, GetErasureCodec());
    Save(context, GetMovable());
    Save(context, GetOverlayed());
    Save(context, GetStripedErasure());
    {
        // COMPAT(shakurov)
        TCompactVector<TChunkTree*, TypicalChunkParentCount> parents;
        for (auto [chunkTree, refCount] : Parents_) {
            for (auto i = 0; i < refCount; ++i) {
                parents.push_back(chunkTree);
            }
        }
        std::sort(parents.begin(), parents.end(), TObjectIdComparer());
        Save(context, parents);
    }
    Save(context, ExpirationTime_);
    if (ReplicasData_) {
        Save(context, true);
        Save(context, *ReplicasData_);
    } else {
        Save(context, false);
    }
    Save(context, ExportCounter_);
    if (ExportCounter_ > 0) {
        YT_ASSERT(ExportDataList_);
        TPodSerializer::Save(context, *ExportDataList_);
    }
    Save(context, EndorsementRequired_);
    Save(context, ConsistentReplicaPlacementHash_);
}

void TChunk::Load(NCellMaster::TLoadContext& context)
{
    TChunkTree::Load(context);

    using NYT::Load;

    Load(context, ChunkMeta_);

    Load(context, AggregatedRequisitionIndex_);
    Load(context, LocalRequisitionIndex_);

    SetReadQuorum(Load<i8>(context));
    SetWriteQuorum(Load<i8>(context));

    Load(context, LogReplicaLagLimit_);

    SetDiskSpace(Load<i64>(context));

    SetErasureCodec(Load<NErasure::ECodec>(context));
    SetMovable(Load<bool>(context));
    SetOverlayed(Load<bool>(context));

    // COMPAT(gritukan)
    if (context.GetVersion() >= EMasterReign::StripedErasureChunks) {
        SetStripedErasure(Load<bool>(context));
    } else {
        SetStripedErasure(false);
    }

    auto parents = Load<TCompactVector<TChunkTree*, TypicalChunkParentCount>>(context);
    for (auto* parent : parents) {
        ++Parents_[parent];
    }

    ExpirationTime_ = Load<TInstant>(context);

    if (Load<bool>(context)) {
        MutableReplicasData()->Load(context);
    }

    Load(context, ExportCounter_);
    if (ExportCounter_ > 0) {
        ExportDataList_ = std::make_unique<TChunkExportDataList>();
        TPodSerializer::Load(context, *ExportDataList_);
        YT_VERIFY(std::any_of(
            ExportDataList_->begin(), ExportDataList_->end(),
            [] (auto data) { return data.RefCounter != 0; }));
    }

    Load(context, EndorsementRequired_);

    Load(context, ConsistentReplicaPlacementHash_);

    if (IsConfirmed()) {
        auto miscExt = ChunkMeta_->GetExtension<TMiscExt>();
        OnMiscExtUpdated(miscExt);
    }
}

void TChunk::AddParent(TChunkTree* parent)
{
    ++Parents_[parent];
}

void TChunk::RemoveParent(TChunkTree* parent)
{
    auto it = Parents_.find(parent);
    YT_VERIFY(it != Parents_.end());
    if (--it->second == 0) {
        Parents_.erase(it);
    }
}

int TChunk::GetParentCount() const
{
    auto result = 0;
    for (auto [parent, cardinality] : Parents_) {
        result += cardinality;
    }
    return result;
}

bool TChunk::HasParents() const
{
    return !Parents_.empty();
}

void TChunk::AddReplica(TNodePtrWithIndexes replica, const TMedium* medium, bool approved)
{
    auto* data = MutableReplicasData();
    if (medium->GetCache()) {
        YT_VERIFY(!IsJournal());
        auto& cachedReplicas = data->CachedReplicas;
        if (!cachedReplicas) {
            cachedReplicas = std::make_unique<TCachedReplicas>();
        }
        YT_VERIFY(cachedReplicas->insert(replica).second);
    } else {
        if (IsJournal()) {
            for (auto& existingReplica : data->MutableStoredReplicas()) {
                if (existingReplica.ToGenericState() == replica.ToGenericState()) {
                    existingReplica = replica;
                    return;
                }
            }
        }

        if (approved) {
            ++data->ApprovedReplicaCount;
        }

        data->AddStoredReplica(replica);
        if (!medium->GetTransient()) {
            auto lastSeenReplicas = data->MutableLastSeenReplicas();
            auto nodeId = replica.GetPtr()->GetId();
            if (IsErasure()) {
                lastSeenReplicas[replica.GetReplicaIndex()] = nodeId;
            } else {
                lastSeenReplicas[data->CurrentLastSeenReplicaIndex] = nodeId;
                data->CurrentLastSeenReplicaIndex = (data->CurrentLastSeenReplicaIndex + 1) % lastSeenReplicas.size();
            }
        }
    }
}

void TChunk::RemoveReplica(TNodePtrWithIndexes replica, const TMedium* medium, bool approved)
{
    auto* data = MutableReplicasData();
    if (medium->GetCache()) {
        auto& cachedReplicas = data->CachedReplicas;
        YT_VERIFY(cachedReplicas->erase(replica) == 1);
        if (cachedReplicas->empty()) {
            cachedReplicas.reset();
        } else {
            ShrinkHashTable(cachedReplicas.get());
        }
    } else {
        if (approved) {
            --data->ApprovedReplicaCount;
            YT_ASSERT(data->ApprovedReplicaCount >= 0);
        }

        auto doRemove = [&] (auto converter) {
            auto storedReplicas = data->GetStoredReplicas();
            for (int replicaIndex = 0; replicaIndex < std::ssize(storedReplicas); ++replicaIndex) {
                auto existingReplica = storedReplicas[replicaIndex];
                if (converter(existingReplica) == converter(replica)) {
                    data->RemoveStoredReplica(replicaIndex);
                    return;
                }
            }
            YT_ABORT();
        };
        if (IsJournal()) {
            doRemove([] (auto replica) { return replica.ToGenericState(); });
        } else {
            doRemove([] (auto replica) { return replica; });
        }
    }
}

TNodePtrWithIndexesList TChunk::GetReplicas(std::optional<int> maxCachedReplicas) const
{
    const auto& storedReplicas = StoredReplicas();
    const auto& cachedReplicas = CachedReplicas();

    TNodePtrWithIndexesList result;
    if (maxCachedReplicas) {
        auto effectiveCachedReplicaCount = std::min<int>(ssize(cachedReplicas), *maxCachedReplicas);
        result.reserve(storedReplicas.size() + effectiveCachedReplicaCount);
        result.insert(result.end(), storedReplicas.begin(), storedReplicas.end());
        auto it = cachedReplicas.begin();
        for (auto i = 0; i < effectiveCachedReplicaCount; ++i) {
            result.push_back(*it++);
        }
    } else {
        result.reserve(storedReplicas.size() + cachedReplicas.size());
        result.insert(result.end(), storedReplicas.begin(), storedReplicas.end());
        result.insert(result.end(), cachedReplicas.begin(), cachedReplicas.end());
    }

    return result;
}

void TChunk::ApproveReplica(TNodePtrWithIndexes replica)
{
    auto* data = MutableReplicasData();
    ++data->ApprovedReplicaCount;

    if (IsJournal()) {
        auto genericReplica = replica.ToGenericState();
        for (auto& existingReplica : data->MutableStoredReplicas()) {
            if (existingReplica.ToGenericState() == genericReplica) {
                existingReplica = replica;
                return;
            }
        }
        YT_ABORT();
    }
}

int TChunk::GetApprovedReplicaCount() const
{
    return ReplicasData().ApprovedReplicaCount;
}

void TChunk::SetApprovedReplicaCount(int count)
{
    MutableReplicasData()->ApprovedReplicaCount = count;
}

void TChunk::Confirm(
    const TChunkInfo& chunkInfo,
    const TChunkMeta& chunkMeta)
{
    // YT-3251
    if (!HasProtoExtension<TMiscExt>(chunkMeta.extensions())) {
        THROW_ERROR_EXCEPTION("Missing TMiscExt in chunk meta");
    }

    Y_UNUSED(CheckedEnumCast<EChunkType>(chunkMeta.type()));
    Y_UNUSED(CheckedEnumCast<EChunkFormat>(chunkMeta.format()));

    ChunkMeta_ = FromProto<TImmutableChunkMetaPtr>(chunkMeta);

    SetDiskSpace(chunkInfo.disk_space());

    auto miscExt = ChunkMeta_->GetExtension<TMiscExt>();
    OnMiscExtUpdated(miscExt);

    YT_VERIFY(IsConfirmed());
}

bool TChunk::GetMovable() const
{
    return Flags_.Movable;
}

void TChunk::SetMovable(bool value)
{
    Flags_.Movable = value;
}

bool TChunk::GetOverlayed() const
{
    return Flags_.Overlayed;
}

void TChunk::SetOverlayed(bool value)
{
    Flags_.Overlayed = value;
}

void TChunk::SetRowCount(i64 rowCount)
{
    YT_VERIFY(IsJournalChunkType(GetType()));

    auto miscExt = ChunkMeta_->GetExtension<TMiscExt>();
    miscExt.set_row_count(rowCount);

    NChunkClient::NProto::TChunkMeta protoMeta;
    ToProto(&protoMeta, ChunkMeta_);
    SetProtoExtension(protoMeta.mutable_extensions(), miscExt);
    ChunkMeta_ = FromProto<TImmutableChunkMetaPtr>(protoMeta);

    OnMiscExtUpdated(miscExt);
}

bool TChunk::IsConfirmed() const
{
    return ChunkMeta_->GetType() != EChunkType::Unknown;
}

bool TChunk::IsAvailable() const
{
    if (!ReplicasData_) {
        // Actually it makes no sense calling IsAvailable for foreign chunks.
        return false;
    }

    const auto& storedReplicas = ReplicasData_->GetStoredReplicas();
    switch (GetType()) {
        case EObjectType::Chunk:
            return !storedReplicas.empty();

        case EObjectType::ErasureChunk:
        case EObjectType::ErasureJournalChunk: {
            auto* codec = NErasure::GetCodec(GetErasureCodec());
            int dataPartCount = codec->GetDataPartCount();
            NErasure::TPartIndexSet missingIndexSet((1 << dataPartCount) - 1);
            for (auto replica : storedReplicas) {
                missingIndexSet.reset(replica.GetReplicaIndex());
            }
            return missingIndexSet.none();
        }

        case EObjectType::JournalChunk: {
            if (std::ssize(storedReplicas) >= GetReadQuorum()) {
                return true;
            }
            for (auto replica : storedReplicas) {
                if (replica.GetState() == EChunkReplicaState::Sealed) {
                    return true;
                }
            }
            return false;
        }

        default:
            YT_ABORT();
    }
}

bool TChunk::IsSealed() const
{
    if (!IsConfirmed()) {
        return false;
    }

    if (!IsJournal()) {
        return true;
    }

    return Flags_.Sealed;
}

void TChunk::SetSealed(bool value)
{
    Flags_.Sealed = value;
}

bool TChunk::GetStripedErasure() const
{
    return Flags_.StripedErasure;
}

void TChunk::SetStripedErasure(bool value)
{
    Flags_.StripedErasure = value;
}

i64 TChunk::GetPhysicalSealedRowCount() const
{
    YT_VERIFY(Flags_.Sealed);
    return GetPhysicalChunkRowCount(GetRowCount(), GetOverlayed());
}

void TChunk::Seal(const TChunkSealInfo& info)
{
    YT_VERIFY(IsConfirmed() && !IsSealed());
    YT_VERIFY(!Flags_.Sealed);
    YT_VERIFY(GetRowCount() == 0);
    YT_VERIFY(GetUncompressedDataSize() == 0);
    YT_VERIFY(GetCompressedDataSize() == 0);
    YT_VERIFY(GetDiskSpace() == 0);

    auto miscExt = ChunkMeta_->GetExtension<TMiscExt>();
    miscExt.set_sealed(true);
    if (info.has_first_overlayed_row_index()) {
        miscExt.set_first_overlayed_row_index(info.first_overlayed_row_index());
    }
    miscExt.set_row_count(info.row_count());
    miscExt.set_uncompressed_data_size(info.uncompressed_data_size());
    miscExt.set_compressed_data_size(info.compressed_data_size());

    NChunkClient::NProto::TChunkMeta protoMeta;
    ToProto(&protoMeta, ChunkMeta_);
    SetProtoExtension(protoMeta.mutable_extensions(), miscExt);
    ChunkMeta_ = FromProto<TImmutableChunkMetaPtr>(protoMeta);

    OnMiscExtUpdated(miscExt);

    SetDiskSpace(info.uncompressed_data_size()); // an approximation
}

int TChunk::GetPhysicalReplicationFactor(int mediumIndex, const TChunkRequisitionRegistry* registry) const
{
    auto mediumReplicationPolicy = GetAggregatedReplication(registry).Get(mediumIndex);
    if (!mediumReplicationPolicy) {
        return 0;
    }

    if (IsErasure()) {
        auto* codec = NErasure::GetCodec(GetErasureCodec());
        return mediumReplicationPolicy.GetDataPartsOnly()
            ? codec->GetDataPartCount()
            : codec->GetTotalPartCount();
    } else {
        return mediumReplicationPolicy.GetReplicationFactor();
    }
}

int TChunk::GetMaxReplicasPerFailureDomain(
    int mediumIndex,
    std::optional<int> replicationFactorOverride,
    const TChunkRequisitionRegistry* registry) const
{
    switch (GetType()) {
        case EObjectType::Chunk: {
            if (replicationFactorOverride) {
                return *replicationFactorOverride;
            }
            auto replicationFactor = GetAggregatedReplicationFactor(mediumIndex, registry);
            return std::max(replicationFactor - 1, 1);
        }

        case EObjectType::ErasureChunk: {
            auto* codec = NErasure::GetCodec(GetErasureCodec());
            return codec->GetGuaranteedRepairablePartCount();
        }

        case EObjectType::JournalChunk:
        case EObjectType::ErasureJournalChunk: {
            YT_ASSERT(!replicationFactorOverride);
            auto replicaCount = GetPhysicalReplicationFactor(mediumIndex, registry);
            // #ReadQuorum replicas are required to read journal chunk, so no more
            // than #replicaCount - #ReadQuorum replicas can be placed in the same rack.
            return std::max(replicaCount - ReadQuorum_, 1);
        }

        default:
            YT_ABORT();
    }
}

TChunkExportData TChunk::GetExportData(int cellIndex) const
{
    if (ExportCounter_ == 0) {
        return {};
    }

    YT_ASSERT(ExportDataList_);
    return (*ExportDataList_)[cellIndex];
}

bool TChunk::IsExportedToCell(int cellIndex) const
{
    if (ExportCounter_ == 0) {
        return false;
    }

    YT_ASSERT(ExportDataList_);
    return (*ExportDataList_)[cellIndex].RefCounter != 0;
}

void TChunk::Export(int cellIndex, TChunkRequisitionRegistry* registry)
{
    if (ExportCounter_ == 0) {
        ExportDataList_ = std::make_unique<TChunkExportDataList>();
        for (auto& data : *ExportDataList_) {
            data.RefCounter = 0;
            data.ChunkRequisitionIndex = EmptyChunkRequisitionIndex;
        }
    }

    auto& data = (*ExportDataList_)[cellIndex];
    if (++data.RefCounter == 1) {
        ++ExportCounter_;

        YT_VERIFY(data.ChunkRequisitionIndex == EmptyChunkRequisitionIndex);
        registry->Ref(data.ChunkRequisitionIndex);
        // NB: an empty requisition doesn't affect the aggregated requisition
        // and thus doesn't call for updating the latter.
    }
}

void TChunk::Unexport(
    int cellIndex,
    int importRefCounter,
    TChunkRequisitionRegistry* registry,
    const NObjectServer::IObjectManagerPtr& objectManager)
{
    YT_ASSERT(ExportDataList_);
    auto& data = (*ExportDataList_)[cellIndex];
    if ((data.RefCounter -= importRefCounter) == 0) {
        registry->Unref(data.ChunkRequisitionIndex, objectManager);
        data.ChunkRequisitionIndex = EmptyChunkRequisitionIndex; // just in case

        --ExportCounter_;

        if (ExportCounter_ == 0) {
            ExportDataList_.reset();
        }

        UpdateAggregatedRequisitionIndex(registry, objectManager);
    }
}

i64 TChunk::GetMasterMemoryUsage() const
{
    auto memoryUsage =
        sizeof(TChunk) +
        sizeof(TChunkDynamicData) +
        ChunkMeta_->GetTotalByteSize();
    if (ReplicasData_) {
        if (IsErasure()) {
            memoryUsage += sizeof(TErasureChunkReplicasData);
        } else {
            memoryUsage += sizeof(TRegularChunkReplicasData);
        }
    }

    return memoryUsage;
}

EChunkType TChunk::GetChunkType() const
{
    return ChunkMeta_->GetType();
}

EChunkFormat TChunk::GetChunkFormat() const
{
    return ChunkMeta_->GetFormat();
}

bool TChunk::HasConsistentReplicaPlacementHash() const
{
    return
        ConsistentReplicaPlacementHash_ != NullConsistentReplicaPlacementHash &&
        !IsErasure(); // CRP with erasure is not supported.
}

void TChunk::OnMiscExtUpdated(const TMiscExt& miscExt)
{
    RowCount_ = miscExt.row_count();
    CompressedDataSize_ = miscExt.compressed_data_size();
    UncompressedDataSize_ = miscExt.uncompressed_data_size();
    DataWeight_ = miscExt.data_weight();
    auto firstOverlayedRowIndex = miscExt.has_first_overlayed_row_index()
        ? std::make_optional(miscExt.first_overlayed_row_index())
        : std::nullopt;
    SetFirstOverlayedRowIndex(firstOverlayedRowIndex);
    MaxBlockSize_ = miscExt.max_data_block_size();
    CompressionCodec_ = FromProto<NCompression::ECodec>(miscExt.compression_codec());
    SystemBlockCount_ = miscExt.system_block_count();
    SetSealed(miscExt.sealed());
    SetStripedErasure(miscExt.striped_erasure());

    if (miscExt.has_physical_row_count()) {
        auto physicalRowCount = GetPhysicalSealedRowCount();
        YT_LOG_FATAL_IF(
            physicalRowCount != miscExt.physical_row_count(),
            "Calculated physical row count does not match the one in misc (CalculatedPhysicalRowCount: %v, MiscPhysicalRowCount: %v)",
            physicalRowCount,
            miscExt.physical_row_count());
    }
}

////////////////////////////////////////////////////////////////////////////////

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
void TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::Initialize()
{
    std::fill(LastSeenReplicas.begin(), LastSeenReplicas.end(), InvalidNodeId);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
TRange<TNodePtrWithIndexes> TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::GetStoredReplicas() const
{
    return MakeRange(StoredReplicas);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
TMutableRange<TNodePtrWithIndexes> TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::MutableStoredReplicas()
{
    return MakeMutableRange(StoredReplicas);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
void TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::AddStoredReplica(TNodePtrWithIndexes replica)
{
    StoredReplicas.push_back(replica);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
void TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::RemoveStoredReplica(int replicaIndex)
{
    std::swap(StoredReplicas[replicaIndex], StoredReplicas.back());
    StoredReplicas.pop_back();
    StoredReplicas.shrink_to_small();
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
TRange<TNodeId> TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::GetLastSeenReplicas() const
{
    return MakeRange(LastSeenReplicas);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
TMutableRange<TNodeId> TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::MutableLastSeenReplicas()
{
    return MakeMutableRange(LastSeenReplicas);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
void TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::Load(TLoadContext& context)
{
    using NYT::Load;

    Load(context, StoredReplicas);
    Load(context, CachedReplicas);
    Load(context, LastSeenReplicas);
    Load(context, CurrentLastSeenReplicaIndex);
    Load(context, ApprovedReplicaCount);
}

template <size_t TypicalStoredReplicaCount, size_t LastSeenReplicaCount>
void TChunk::TReplicasData<TypicalStoredReplicaCount, LastSeenReplicaCount>::Save(TSaveContext& context) const
{
    using NYT::Save;

    // NB: RemoveReplica calls do not commute and their order is not
    // deterministic (i.e. when unregistering a node we traverse certain hashtables).
    TVectorSerializer<TDefaultSerializer, TSortedTag>::Save(context, StoredReplicas);
    Save(context, CachedReplicas);
    Save(context, LastSeenReplicas);
    Save(context, CurrentLastSeenReplicaIndex);
    Save(context, ApprovedReplicaCount);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
