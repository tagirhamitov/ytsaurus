#include "stdafx.h"
#include "blob_chunk.h"
#include "private.h"
#include "location.h"
#include "blob_reader_cache.h"
#include "chunk_cache.h"
#include "block_store.h"

#include <core/profiling/scoped_timer.h>

#include <core/misc/fs.h>

#include <ytlib/chunk_client/file_reader.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <server/cell_node/bootstrap.h>
#include <server/cell_node/config.h>

namespace NYT {
namespace NDataNode {

using namespace NCellNode;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = DataNodeLogger;
static auto& Profiler = DataNodeProfiler;

static NProfiling::TRateCounter DiskBlobReadThroughputCounter("/disk_blob_read_throughput");

////////////////////////////////////////////////////////////////////////////////

TBlobChunkBase::TBlobChunkBase(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info,
    const TChunkMeta* meta)
    : TChunkBase(
        bootstrap,
        location,
        id,
        info)
{
    if (meta) {
        InitializeCachedMeta(*meta);
    }
}

TBlobChunkBase::~TBlobChunkBase()
{
    if (Meta_) {
        auto* tracker = Bootstrap_->GetMemoryUsageTracker();
        tracker->Release(EMemoryConsumer::ChunkMeta, Meta_->SpaceUsed());
    }
}

IChunk::TAsyncGetMetaResult TBlobChunkBase::GetMeta(
    i64 priority,
    const std::vector<int>* tags)
{
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (Meta_) {
            guard.Release();
            LOG_DEBUG("Meta cache hit (ChunkId: %s)", ~ToString(Id_));
            return MakeFuture(TGetMetaResult(FilterCachedMeta(tags)));
        }
    }

    LOG_DEBUG("Meta cache miss (ChunkId: %s)", ~ToString(Id_));

    // Make a copy of tags list to pass it into the closure.
    auto tags_ = MakeNullable(tags);
    auto this_ = MakeStrong(this);
    auto invoker = Bootstrap_->GetControlInvoker();
    return ReadMeta(priority).Apply(
        BIND([=] (TError error) -> TGetMetaResult {
            if (!error.IsOK()) {
                return error;
            }
            return FilterCachedMeta(tags);
        }).AsyncVia(invoker));
}

IChunk::TAsyncReadBlocksResult TBlobChunkBase::ReadBlocks(
    int firstBlockIndex,
    int blockCount,
    i64 priority)
{
    YCHECK(firstBlockIndex >= 0);
    YCHECK(blockCount >= 0);

    i64 pendingSize;
    AdjustReadRange(firstBlockIndex, &blockCount, &pendingSize);

    if (pendingSize >= 0) {
        auto blockStore = Bootstrap_->GetBlockStore();
        blockStore->UpdatePendingReadSize(+pendingSize);
    }

    auto promise = NewPromise<TReadBlocksResult>();

    auto callback = BIND(
        &TBlobChunkBase::DoReadBlocks,
        MakeStrong(this),
        firstBlockIndex,
        blockCount,
        pendingSize,
        promise);

    Location_
        ->GetDataReadInvoker()
        ->Invoke(callback, priority);

    return promise;
}

void TBlobChunkBase::DoReadBlocks(
    int firstBlockIndex,
    int blockCount,
    i64 pendingSize,
    TPromise<TReadBlocksResult> promise)
{
    auto blockStore = Bootstrap_->GetBlockStore();
    auto readerCache = Bootstrap_->GetBlobReaderCache();

    try {
        auto reader = readerCache->GetReader(this);

        if (pendingSize < 0) {
            InitializeCachedMeta(reader->GetMeta());
            AdjustReadRange(firstBlockIndex, &blockCount, &pendingSize);
            YCHECK(pendingSize >= 0);
        }

        std::vector<TSharedRef> blocks;

        LOG_DEBUG("Started reading blob chunk blocks (BlockIds: %s:%d-%d, LocationId: %s)",
            ~ToString(Id_),
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            ~Location_->GetId());
            
        NProfiling::TScopedTimer timer;

        // NB: The reader is synchronous.
        auto blocksOrError = reader->ReadBlocks(firstBlockIndex, blockCount).Get();

        auto readTime = timer.GetElapsed();

        LOG_DEBUG("Finished reading blob chunk blocks (BlockIds: %s:%d-%d, LocationId: %s)",
            ~ToString(Id_),
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            ~Location_->GetId());

        if (!blocksOrError.IsOK()) {
            Location_->Disable();
            THROW_ERROR_EXCEPTION(
                NChunkClient::EErrorCode::IOError,
                "Error reading blob chunk %s",
                ~ToString(Id_))
                << TError(blocksOrError);
        }

        auto& locationProfiler = Location_->Profiler();
        locationProfiler.Enqueue("/blob_block_read_size", pendingSize);
        locationProfiler.Enqueue("/blob_block_read_time", readTime.MicroSeconds());
        locationProfiler.Enqueue("/blob_block_read_throughput", pendingSize * 1000000 / (1 + readTime.MicroSeconds()));
        DataNodeProfiler.Increment(DiskBlobReadThroughputCounter, pendingSize);

        YCHECK(pendingSize >= 0);
        blockStore->UpdatePendingReadSize(-pendingSize);

        promise.Set(blocksOrError.Value());
    } catch (const std::exception& ex) {
        if (pendingSize >= 0) {
            blockStore->UpdatePendingReadSize(-pendingSize);
        }
        promise.Set(ex);
    }
}

TAsyncError TBlobChunkBase::ReadMeta(i64 priority)
{
    if (!TryAcquireReadLock()) {
        return MakeFuture(TError("Cannot read meta of chunk %s: chunk is scheduled for removal",
            ~ToString(Id_)));
    }

    auto promise = NewPromise<TError>();
    auto callback = BIND(&TBlobChunkBase::DoReadMeta, MakeStrong(this), promise);
    Location_
        ->GetMetaReadInvoker()
        ->Invoke(callback, priority);
    return promise;
}

void TBlobChunkBase::DoReadMeta(TPromise<TError> promise)
{
    auto& Profiler = Location_->Profiler();
    LOG_DEBUG("Started reading chunk meta (ChunkId: %s, LocationId: %s)",
        ~ToString(Id_),
        ~Location_->GetId());

    NChunkClient::TFileReaderPtr reader;
    PROFILE_TIMING ("/meta_read_time") {
        auto readerCache = Bootstrap_->GetBlobReaderCache();
        try {
            reader = readerCache->GetReader(this);
        } catch (const std::exception& ex) {
            ReleaseReadLock();
            LOG_WARNING(ex, "Error reading chunk meta (ChunkId: %s)",
                ~ToString(Id_));
            promise.Set(ex);
            return;
        }
    }

    InitializeCachedMeta(reader->GetMeta());
    ReleaseReadLock();

    LOG_DEBUG("Finished reading chunk meta (ChunkId: %s, LocationId: %s)",
        ~ToString(Id_),
        ~Location_->GetId());

    promise.Set(TError());
}

void TBlobChunkBase::InitializeCachedMeta(const NChunkClient::NProto::TChunkMeta& meta)
{
    TGuard<TSpinLock> guard(SpinLock_);
    // This check is important since this code may get triggered
    // multiple times and readers do not use any locking.
    if (Meta_)
        return;

    BlocksExt_ = GetProtoExtension<TBlocksExt>(meta.extensions());
    Meta_ = New<TRefCountedChunkMeta>(meta);

    auto* tracker = Bootstrap_->GetMemoryUsageTracker();
    tracker->Acquire(EMemoryConsumer::ChunkMeta, Meta_->SpaceUsed());
}

void TBlobChunkBase::AdjustReadRange(
    int firstBlockIndex,
    int* blockCount,
    i64* dataSize)
{
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (!Meta_) {
            *dataSize = -1;
            return;
        }
    }

    auto config = Bootstrap_->GetConfig()->DataNode;
    *blockCount = std::min(*blockCount, config->MaxBlocksPerRead);

    *dataSize = 0;
    int blockIndex = 0;
    while (
        blockIndex < firstBlockIndex + *blockCount &&
        blockIndex < BlocksExt_.blocks_size() &&
        *dataSize <= config->MaxBytesPerRead)
    {
        const auto& blockInfo = BlocksExt_.blocks(blockIndex);
        *dataSize += blockInfo.size();
        ++blockIndex;
    }

    *blockCount = blockIndex - firstBlockIndex;
}

void TBlobChunkBase::EvictFromCache()
{
    auto readerCache = Bootstrap_->GetBlobReaderCache();
    readerCache->EvictReader(this);
}

TFuture<void> TBlobChunkBase::RemoveFiles()
{
    auto dataFileName = GetFileName();
    auto metaFileName = dataFileName + ChunkMetaSuffix;
    auto id = Id_;
    auto location = Location_;

    return BIND([=] () {
        LOG_DEBUG("Started removing blob chunk files (ChunkId: %s)",
            ~ToString(id));

        try {
            NFS::Remove(dataFileName);
            NFS::Remove(metaFileName);
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error removing blob chunk files");
            location->Disable();
        }

        LOG_DEBUG("Finished removing blob chunk files (ChunkId: %s)",
            ~ToString(id));
    }).AsyncVia(location->GetWriteInvoker()).Run();
}

////////////////////////////////////////////////////////////////////////////////

TStoredBlobChunk::TStoredBlobChunk(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info,
    const TChunkMeta* meta)
    : TBlobChunkBase(
        bootstrap,
        location,
        id,
        info,
        meta)
{ }

////////////////////////////////////////////////////////////////////////////////

TCachedBlobChunk::TCachedBlobChunk(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info,
    const TChunkMeta* meta)
    : TBlobChunkBase(
        bootstrap,
        location,
        id,
        info,
        meta)
    , TCacheValueBase<TChunkId, TCachedBlobChunk>(GetId())
    , ChunkCache_(Bootstrap_->GetChunkCache())
{ }

TCachedBlobChunk::~TCachedBlobChunk()
{
    // This check ensures that we don't remove any chunks from cache upon shutdown.
    if (!ChunkCache_.IsExpired()) {
        LOG_INFO("Chunk is evicted from cache (ChunkId: %s)", ~ToString(GetId()));
        EvictFromCache();
        RemoveFiles();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
