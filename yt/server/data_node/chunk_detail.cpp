#include "stdafx.h"
#include "chunk_detail.h"
#include "private.h"
#include "location.h"

#include <server/cell_node/bootstrap.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>

namespace NYT {
namespace NDataNode {

using namespace NCellNode;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

TChunkBase::TChunkBase(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info)
    : Bootstrap_(bootstrap)
    , Location_(location)
    , Id_(id)
    , Info_(info)
{ }

const TChunkId& TChunkBase::GetId() const
{
    return Id_;
}

TLocationPtr TChunkBase::GetLocation() const
{
    return Location_;
}

const TChunkInfo& TChunkBase::GetInfo() const
{
    return Info_;
}

Stroka TChunkBase::GetFileName() const
{
    return Location_->GetChunkFileName(Id_);
}

bool TChunkBase::TryAcquireReadLock()
{
    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedEvent_) {
            LOG_DEBUG("Chunk read lock cannot be acquired since removal is already pending (ChunkId: %s)",
                ~ToString(Id_));
            return false;
        }

        lockCount = ++ReadLockCounter_;
    }

    LOG_DEBUG("Chunk read lock acquired (ChunkId: %s, LockCount: %d)",
        ~ToString(Id_),
        lockCount);

    return true;
}

void TChunkBase::ReleaseReadLock()
{
    bool scheduleRemoval = false;
    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        YCHECK(ReadLockCounter_ > 0);
        lockCount = --ReadLockCounter_;
        if (ReadLockCounter_ == 0 && !RemovalScheduled_ && RemovedEvent_) {
            scheduleRemoval = RemovalScheduled_ = true;
        }
    }

    LOG_DEBUG("Chunk read lock released (ChunkId: %s, LockCount: %d)",
        ~ToString(Id_),
        lockCount);

    if (scheduleRemoval) {
        DoRemove();
    }
}

bool TChunkBase::IsReadLockAcquired() const
{
    return ReadLockCounter_ > 0;
}

TFuture<void> TChunkBase::ScheduleRemoval()
{
    LOG_INFO("Chunk removal scheduled (ChunkId: %s)",
        ~ToString(Id_));

    bool scheduleRemoval = false;

    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedEvent_) {
            return RemovedEvent_;
        }

        RemovedEvent_ = NewPromise();
        if (ReadLockCounter_ == 0 && !RemovalScheduled_) {
            scheduleRemoval = RemovalScheduled_ = true;
        }
    }

    if (scheduleRemoval) {
        DoRemove();
    }

    return RemovedEvent_;
}

void TChunkBase::DoRemove()
{
    EvictFromCache();

    auto this_ = MakeStrong(this);
    RemoveFiles().Subscribe(BIND([=] () {
        this_->RemovedEvent_.Set();
    }));
}

TRefCountedChunkMetaPtr TChunkBase::FilterCachedMeta(const std::vector<int>* tags) const
{
    YCHECK(Meta_);
    return tags
        ? New<TRefCountedChunkMeta>(FilterChunkMetaByExtensionTags(*Meta_, *tags))
        : Meta_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
