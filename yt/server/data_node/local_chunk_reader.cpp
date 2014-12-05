#include "stdafx.h"
#include "local_chunk_reader.h"

#include <core/concurrency/parallel_awaiter.h>

#include <core/tracing/trace_context.h>

#include <ytlib/chunk_client/config.h>
#include <ytlib/chunk_client/chunk_reader.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <ytlib/table_client/chunk_meta_extensions.h>

#include <server/data_node/chunk.h>
#include <server/data_node/block_store.h>

#include <server/cell_node/bootstrap.h>

namespace NYT {
namespace NDataNode {

using namespace NConcurrency;
using namespace NChunkClient;
using namespace NTableClient;
using namespace NDataNode;
using namespace NCellNode;

////////////////////////////////////////////////////////////////////////////////

class TLocalChunkReader;
typedef TIntrusivePtr<TLocalChunkReader> TLocalChunkReaderPtr;

class TLocalChunkReader
    : public NChunkClient::IChunkReader
{
public:
    TLocalChunkReader(
        TBootstrap* bootstrap,
        TReplicationReaderConfigPtr config,
        IChunkPtr chunk)
        : Bootstrap_(bootstrap)
        , Config_(config)
        , Chunk_(chunk)
    { }

    virtual TAsyncReadBlocksResult ReadBlocks(const std::vector<int>& blockIndexes) override
    {
        NTracing::TTraceSpanGuard guard(
            // XXX(sandello): Disable tracing due to excessive output.
            NTracing::NullTraceContext, /* NTracing::GetCurrentTraceContext(), */
            "LocalChunkReader",
            "ReadBlocks");
        return New<TReadSession>(this, std::move(guard))
            ->Run(blockIndexes);
    }

    virtual TAsyncReadBlocksResult ReadBlocks(int firstBlockIndex, int blockCount) override
    {
        // TODO(babenko): implement when first needed
        YUNIMPLEMENTED();
    }

    virtual TAsyncGetMetaResult GetMeta(
        const TNullable<int>& partitionTag,
        const std::vector<int>* extensionTags) override
    {
        NTracing::TTraceSpanGuard guard(
            // XXX(sandello): Disable tracing due to excessive output.
            NTracing::NullTraceContext, /* NTracing::GetCurrentTraceContext(), */
            "LocalChunkReader",
            "GetChunkMeta");
        return Chunk_
            ->GetMeta(0, extensionTags)
            .Apply(BIND(
                &TLocalChunkReader::OnGotChunkMeta,
                partitionTag,
                Passed(std::move(guard))));
    }

    virtual TChunkId GetChunkId() const override
    {
        return Chunk_->GetId();
    }

private:
    TBootstrap* Bootstrap_;
    TReplicationReaderConfigPtr Config_;
    IChunkPtr Chunk_;


    class TReadSession
        : public TIntrinsicRefCounted
    {
    public:
        TReadSession(TLocalChunkReaderPtr owner, NTracing::TTraceSpanGuard guard)
            : Owner_(owner)
            , Promise_(NewPromise<TErrorOr<std::vector<TSharedRef>>>())
            , TraceSpanGuard_(std::move(guard))
        { }

        TAsyncReadBlocksResult Run(const std::vector<int>& blockIndexes)
        {
            Blocks_.resize(blockIndexes.size());

            auto blockStore = Owner_->Bootstrap_->GetBlockStore();
            auto awaiter = New<TParallelAwaiter>(GetSyncInvoker());
            i64 priority = 0;
            for (int index = 0; index < static_cast<int>(blockIndexes.size()); ++index) {
                auto asyncResult = BIND(
                    &TBlockStore::FindBlock,
                    blockStore,
                    Owner_->Chunk_->GetId(),
                    blockIndexes[index],
                    priority,
                    Owner_->Config_->EnableCaching);
                auto handler = BIND(
                    &TReadSession::OnBlockFound,
                    MakeStrong(this),
                    index,
                    blockIndexes[index]);
                awaiter->Await(
                    asyncResult
                        .AsyncVia(Owner_->Bootstrap_->GetControlInvoker())
                        .Run(),
                    handler);
                // Assign decreasing priorities to block requests to take advantage of sequential read.
                --priority;
            }
    
            awaiter->Complete(BIND(&TReadSession::OnCompleted, MakeStrong(this)));

            return Promise_;
        }

    private:
        TLocalChunkReaderPtr Owner_;
        TPromise<TErrorOr<std::vector<TSharedRef>>> Promise_;

        NTracing::TTraceSpanGuard TraceSpanGuard_;

        std::vector<TSharedRef> Blocks_;


        void OnBlockFound(int index, int blockIndex, TBlockStore::TGetBlockResult result)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            if (!result.IsOK()) {
                Promise_.TrySet(TError(
                    NDataNode::EErrorCode::LocalChunkReaderFailed,
                    "Error reading local chunk block %v:%v",
                    Owner_->Chunk_->GetId(),
                    blockIndex)
                    << result);
                return;
            }

            if (!result.Value()) {
                Promise_.TrySet(TError(
                    NDataNode::EErrorCode::LocalChunkReaderFailed,
                    "Local chunk block %v:%v is not available",
                    Owner_->Chunk_->GetId(),
                    blockIndex));
                return;
            }

            Blocks_[index] = result.Value();
        }

        void OnCompleted()
        {
            VERIFY_THREAD_AFFINITY_ANY();

            TraceSpanGuard_.Release();
            Promise_.TrySet(Blocks_);
        }

    };

    static TGetMetaResult OnGotChunkMeta(
        const TNullable<int>& partitionTag,
        NTracing::TTraceSpanGuard /*guard*/,
        IChunk::TGetMetaResult result)
    {
        if (!result.IsOK()) {
            return TError(result);
        }

        const auto& chunkMeta = *result.Value();
        return partitionTag
            ? TGetMetaResult(FilterChunkMetaByPartitionTag(chunkMeta, *partitionTag))
            : TGetMetaResult(chunkMeta);
    }

};

NChunkClient::IChunkReaderPtr CreateLocalChunkReader(
    TBootstrap* bootstrap,
    TReplicationReaderConfigPtr config,
    IChunkPtr chunk)
{
    return New<TLocalChunkReader>(
        bootstrap,
        config,
        chunk);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
