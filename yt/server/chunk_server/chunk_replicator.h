#pragma once

#include "private.h"
#include "chunk.h"
#include "chunk_replica.h"

#include <yt/server/cell_master/public.h>

#include <yt/server/node_tracker_server/rack.h>

#include <yt/ytlib/chunk_client/chunk_replica.h>

#include <yt/ytlib/node_tracker_client/node_tracker_service.pb.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/erasure/public.h>

#include <yt/core/misc/error.h>
#include <yt/core/misc/nullable.h>
#include <yt/core/misc/property.h>

#include <yt/core/profiling/timing.h>

#include <functional>
#include <deque>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkReplicator
    : public TRefCounted
{
public:
    TChunkReplicator(
        TChunkManagerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap,
        TChunkPlacementPtr chunkPlacement);

    void Start();
    void Stop();

    void OnNodeRegistered(TNode* node);
    void OnNodeUnregistered(TNode* node);
    void OnNodeDisposed(TNode* node);

    // 'On all of the media' chunk states. E.g. LostChunks contain chunks that
    // have been lost on all of the media.
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, LostChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, LostVitalChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, DataMissingChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, ParityMissingChunks);
    // Medium-wise unsafely placed chunks: all replicas are on transient media
    // (and properties of these chunks demand otherwise).
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, PrecariousChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, PrecariousVitalChunks);


    // 'On any medium'. E.g. UnderreplicatedChunks contain chunks that are
    // underreplicated on at least one medium.
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, UnderreplicatedChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, OverreplicatedChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, QuorumMissingChunks);
    // Rack-wise unsafely placed chunks.
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, UnsafelyPlacedChunks);

    void OnChunkDestroyed(TChunk* chunk);
    void OnReplicaRemoved(TNode* node, TChunkPtrWithIndexes chunkWithIndexes, ERemoveReplicaReason reason);

    void ScheduleChunkRefresh(const TChunkId& chunkId);
    void ScheduleChunkRefresh(TChunk* chunk);
    void ScheduleNodeRefresh(TNode* node);

    void ScheduleUnknownReplicaRemoval(TNode* node, const NChunkClient::TChunkIdWithIndexes& chunkdIdWithIndexes);
    void ScheduleReplicaRemoval(TNode* node, TChunkPtrWithIndexes chunkWithIndexes);

    void SchedulePropertiesUpdate(TChunkTree* chunkTree);
    void SchedulePropertiesUpdate(TChunk* chunk);
    void SchedulePropertiesUpdate(TChunkList* chunkList);

    void TouchChunk(TChunk* chunk);

    TJobPtr FindJob(const TJobId& id);

    TPerMediumArray<EChunkStatus> ComputeChunkStatuses(TChunk* chunk);

    void ScheduleJobs(
        TNode* node,
        const std::vector<TJobPtr>& currentJobs,
        std::vector<TJobPtr>* jobsToStart,
        std::vector<TJobPtr>* jobsToAbort,
        std::vector<TJobPtr>* jobsToRemove);

    bool IsEnabled();

    int GetRefreshListSize() const;
    int GetPropertiesUpdateListSize() const;

private:
    struct TPerMediumChunkStatistics
    {
        TPerMediumChunkStatistics();

        EChunkStatus Status;

        //! Number of active replicas, per each replica index.
        int ReplicaCount[NChunkClient::ChunkReplicaIndexBound];

        //! Number of decommissioned replicas, per each replica index.
        int DecommissionedReplicaCount[NChunkClient::ChunkReplicaIndexBound];


        //! Indexes of replicas whose replication is advised.
        SmallVector<int, TypicalReplicaCount> ReplicationIndexes;

        //! Decommissioned replicas whose removal is advised.
        // NB: there's no actual need to have medium index in context of this
        // per-medium class. This is just for convenience.
        SmallVector<TNodePtrWithIndexes, TypicalReplicaCount> DecommissionedRemovalReplicas;
         //! Indexes of replicas whose removal is advised for balancing.
        SmallVector<int, TypicalReplicaCount> BalancingRemovalIndexes;
    };

    struct TChunkStatistics
    {
        TPerMediumArray<TPerMediumChunkStatistics> PerMediumStatistics;
        // Aggregate status across all media. The only applicable statuses are:
        //   Lost - the chunk is lost on all media;
        //   DataMissing - the chunk is missing data parts on all media;
        //   ParityMissing - the chunk is missing parity parts on all media;
        //   Precarious - the chunk has no replicas on a non-transient media;
        //   MediumWiseLost - the chunk is lost on some media, but not on others.
        ECrossMediumChunkStatus Status = ECrossMediumChunkStatus::None;
    };

    struct TRefreshEntry
    {
        TChunk* Chunk = nullptr;
        NProfiling::TCpuInstant When;
    };

    const TChunkManagerConfigPtr Config_;
    NCellMaster::TBootstrap* const Bootstrap_;
    const TChunkPlacementPtr ChunkPlacement_;

    NProfiling::TCpuDuration ChunkRefreshDelay_;

    NConcurrency::TPeriodicExecutorPtr RefreshExecutor_;
    std::deque<TRefreshEntry> RefreshList_;

    NConcurrency::TPeriodicExecutorPtr PropertiesUpdateExecutor_;
    std::deque<TChunk*> PropertiesUpdateList_;

    yhash_map<TJobId, TJobPtr> JobMap_;

    TChunkRepairQueue ChunkRepairQueue_;

    NConcurrency::TPeriodicExecutorPtr EnabledCheckExecutor_;
    bool Enabled_ = false;

    NConcurrency::IThroughputThrottlerPtr JobThrottler_;


    void ProcessExistingJobs(
        TNode* node,
        const std::vector<TJobPtr>& currentJobs,
        std::vector<TJobPtr>* jobsToAbort,
        std::vector<TJobPtr>* jobsToRemove);

    TJobId GenerateJobId();
    bool CreateReplicationJob(
        TNode* sourceNode,
        TChunkPtrWithIndexes chunkWithIndex,
        TJobPtr* job);
    bool CreateBalancingJob(
        TNode* sourceNode,
        TChunkPtrWithIndexes chunkWithIndex,
        double maxFillCoeff,
        TJobPtr* jobsToStart);
    bool CreateRemovalJob(
        TNode* node,
        const NChunkClient::TChunkIdWithIndexes& chunkIdWithIndex,
        TJobPtr* job);
    bool CreateRepairJob(
        TNode* node,
        TChunk* chunk,
        int mediumIndex,
        TJobPtr* job);
    bool CreateSealJob(
        TNode* node,
        TChunk* chunk,
        TJobPtr* job);
    void ScheduleNewJobs(
        TNode* node,
        std::vector<TJobPtr>* jobsToStart,
        std::vector<TJobPtr>* jobsToAbort);

    void OnRefresh();
    void RefreshChunk(TChunk* chunk);

    void ResetChunkStatus(TChunk* chunk);
    void RemoveChunkFromQueues(TChunk* chunk, int mediumIndex, bool dropRemovals);
    void RemoveReplicaFromQueues(TChunk* chunk, TNodePtrWithIndexes nodeWithIndexes, bool dropRemovals);
    void CancelChunkJobs(TChunk* chunk);

    TChunkStatistics ComputeChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeRegularChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeErasureChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeJournalChunkStatistics(TChunk* chunk);

    // Implementation details for Compute{Regular,Erasure}ChunkStatistics().
    void ComputeRegularChunkStatisticsForMedium(
        TPerMediumChunkStatistics& result,
        int replicationFactor,
        int replicaCount,
        int decommissionedReplicaCount,
        const TNodePtrWithIndexesList& decommissionedReplicas,
        bool hasUnsafelyPlacedReplicas);
    void ComputeRegularChunkStatisticsCrossMedia(
        TChunkStatistics& result,
        bool precarious,
        bool allMediaTransient,
        const SmallVector<int, MaxMediumCount>& mediaOnWhichLost,
        int mediaOnWhichPresentCount);
    void ComputeErasureChunkStatisticsForMedium(
        TPerMediumChunkStatistics& result,
        NErasure::ICodec* codec,
        int replicationFactor,
        std::array<TNodePtrWithIndexesList, NChunkClient::ChunkReplicaIndexBound>& decommissionedReplicas,
        int unsafelyPlacedReplicaIndex,
        NErasure::TPartIndexSet& erasedIndexes,
        bool dataPartsOnly);
    void ComputeErasureChunkStatisticsCrossMedia(
        TChunkStatistics& result,
        NErasure::ICodec* codec,
        bool allMediaTransient,
        bool allMediaDataPartsOnly,
        const TPerMediumArray<NErasure::TPartIndexSet>& mediumToErasedIndexes);

    bool IsReplicaDecommissioned(TNodePtrWithIndexes replica);

    void OnPropertiesUpdate();

    //! Computes the actual properties the chunk must have.
    TChunkProperties ComputeChunkProperties(TChunk* chunk);

    //! Follows upward parent links.
    //! Stops when some owning nodes are discovered or parents become ambiguous.
    TChunkList* FollowParentLinks(TChunkList* chunkList);

    void RegisterJob(const TJobPtr& job);

    void UnregisterJob(const TJobPtr& job, EJobUnregisterFlags flags = EJobUnregisterFlags::All);

    void AddToChunkRepairQueue(TChunk* chunk, int mediumIndex);
    void RemoveFromChunkRepairQueue(TChunk* chunk);

    void OnCheckEnabled();
    void OnCheckEnabledPrimary();
    void OnCheckEnabledSecondary();

};

DEFINE_REFCOUNTED_TYPE(TChunkReplicator)

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
