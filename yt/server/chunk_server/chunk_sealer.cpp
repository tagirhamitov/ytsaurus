#include "stdafx.h"
#include "chunk_sealer.h"
#include "chunk.h"
#include "chunk_list.h"
#include "chunk_manager.h"
#include "chunk_owner_base.h"
#include "helpers.h"
#include "config.h"
#include "private.h"

#include <core/concurrency/periodic_executor.h>
#include <core/concurrency/delayed_executor.h>
#include <core/concurrency/async_semaphore.h>
#include <core/concurrency/scheduler.h>

#include <core/ytree/ypath_client.h>

#include <ytlib/journal_client/helpers.h>

#include <ytlib/node_tracker_client/node_directory.h>

#include <ytlib/chunk_client/chunk_ypath_proxy.h>

#include <ytlib/object_client/helpers.h>

#include <server/node_tracker_server/node.h>

#include <server/cell_master/automaton.h>
#include <server/cell_master/meta_state_facade.h>
#include <server/cell_master/bootstrap.h>

#include <deque>

namespace NYT {
namespace NChunkServer {

using namespace NConcurrency;
using namespace NYTree;
using namespace NObjectClient;
using namespace NJournalClient;
using namespace NNodeTrackerClient;
using namespace NChunkClient;
using namespace NObjectClient;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = ChunkServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TChunkSealer::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TChunkManagerConfigPtr config,
        TBootstrap* bootstrap)
        : Config_(config)
        , Bootstrap_(bootstrap)
        , Semaphore_(Config_->MaxChunkConcurrentSeals)
    { }

    void Initialize()
    {
        auto chunkManager = Bootstrap_->GetChunkManager();
        for (auto* chunk : chunkManager->Chunks().GetValues()) {
            if (chunk->IsJournal()) {
                ScheduleSeal(chunk);
            }
        }

        auto metaStateFacade = Bootstrap_->GetMetaStateFacade();
        RefreshExecutor_ = New<TPeriodicExecutor>(
            metaStateFacade->GetEpochInvoker(),
            BIND(&TImpl::OnRefresh, MakeWeak(this)),
            Config_->ChunkRefreshPeriod);
        RefreshExecutor_->Start();
    }

    void ScheduleSeal(TChunk* chunk)
    {
        YASSERT(chunk->IsAlive());
        YASSERT(chunk->IsJournal());

        if (IsSealNeeded(chunk)) {
            EnqueueChunk(chunk);
        }
    }

private:
    TBootstrap* Bootstrap_;
    TChunkManagerConfigPtr Config_;

    TAsyncSemaphore Semaphore_;

    TPeriodicExecutorPtr RefreshExecutor_;

    std::deque<TChunk*> SealQueue_;


    static bool IsSealNeeded(TChunk* chunk)
    {
        return
            chunk->IsAlive() &&
            !chunk->IsSealed();
    }

    static bool IsLocked(TChunk* chunk)
    {
        for (auto* parent : chunk->Parents()) {
            auto nodes = GetOwningNodes(parent);
            for (auto* node : nodes) {
                if (node->GetUpdateMode() != EUpdateMode::None) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool HasEnoughReplicas(TChunk* chunk)
    {
        return chunk->StoredReplicas().size() >= chunk->GetReadQuorum();
    }

    static bool CanSeal(TChunk* chunk)
    {
        return
            IsSealNeeded(chunk) &&
            HasEnoughReplicas(chunk) &&
            !IsLocked(chunk);
    }


    void RescheduleSeal(TChunk* chunk)
    {
        if (IsSealNeeded(chunk)) {
            EnqueueChunk(chunk);
        }
        EndDequeueChunk(chunk);
    }

    void EnqueueChunk(TChunk* chunk)
    {
        if (chunk->GetSealScheduled())
            return;

        auto objectManager = Bootstrap_->GetObjectManager();
        objectManager->WeakRefObject(chunk);

        SealQueue_.push_back(chunk);
        chunk->SetSealScheduled(true);
    }

    TChunk* BeginDequeueChunk()
    {
        if (SealQueue_.empty()) {
            return nullptr;
        }
        auto* chunk = SealQueue_.front();
        SealQueue_.pop_front();
        chunk->SetSealScheduled(false);
        return chunk;
    }

    void EndDequeueChunk(TChunk* chunk)
    {
        auto objectManager = Bootstrap_->GetObjectManager();
        objectManager->WeakUnrefObject(chunk);
    }


    void OnRefresh()
    {
        int chunksDequeued = 0;
        while (true) {
            auto guard = TAsyncSemaphoreGuard::TryAcquire(&Semaphore_);
            if (!guard)
                return;

            while (true) {
                if (chunksDequeued >= Config_->MaxChunksPerRefresh)
                    return;

                auto* chunk = BeginDequeueChunk();
                if (!chunk)
                    return;

                ++chunksDequeued;

                if (!CanSeal(chunk)) {
                    EndDequeueChunk(chunk);
                    continue;
                }

                BIND(&TImpl::SealChunk, MakeStrong(this), chunk, Passed(std::move(guard)))
                    .AsyncVia(GetCurrentInvoker())
                    .Run();
            }

        }
    }

    void SealChunk(
        TChunk* chunk,
        TAsyncSemaphoreGuard /*guard*/)
    {
        if (CanSeal(chunk)) {
            try {
                GuardedSealChunk(chunk);
                EndDequeueChunk(chunk);
            } catch (const std::exception& ex) {
                LOG_WARNING(ex, "Error sealing journal chunk %s, backing off",
                    ~ToString(chunk->GetId()));
                TDelayedExecutor::Submit(
                    BIND(&TImpl::RescheduleSeal, MakeStrong(this), chunk)
                        .Via(GetCurrentInvoker()),
                    Config_->ChunkSealBackoffTime);
            }
        }
    }

    void GuardedSealChunk(TChunk* chunk)
    {
        LOG_INFO("Sealing journal chunk (ChunkId: %s)",
            ~ToString(chunk->GetId()));

        std::vector<TNodeDescriptor> replicas;
        for (auto nodeWithIndex : chunk->StoredReplicas()) {
            auto* node = nodeWithIndex.GetPtr();
            replicas.push_back(node->GetDescriptor());
        }

        {
            auto result = WaitFor(AbortSessionsQuorum(
                chunk->GetId(),
                replicas,
                Config_->JournalRpcTimeout,
                chunk->GetReadQuorum()));
            THROW_ERROR_EXCEPTION_IF_FAILED(result);
        }

        int recordCount;
        {
            auto result = WaitFor(ComputeQuorumRecordCount(
                chunk->GetId(),
                replicas,
                Config_->JournalRpcTimeout,
                chunk->GetReadQuorum()));
            THROW_ERROR_EXCEPTION_IF_FAILED(result);
            recordCount = result.Value();
        }

        auto objectManager = Bootstrap_->GetObjectManager();
        auto rootService = objectManager->GetRootService();
        auto chunkProxy = objectManager->GetProxy(chunk);
        auto req = TChunkYPathProxy::Seal(FromObjectId(chunk->GetId()));
        req->set_record_count(recordCount);
        ExecuteVerb(rootService, req);
    }


};

////////////////////////////////////////////////////////////////////////////////

TChunkSealer::TChunkSealer(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Impl_(New<TImpl>(config, bootstrap))
{ }

TChunkSealer::~TChunkSealer()
{ }

void TChunkSealer::Initialize()
{
    Impl_->Initialize();
}

void TChunkSealer::ScheduleSeal(TChunk* chunk)
{
    Impl_->ScheduleSeal(chunk);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT

