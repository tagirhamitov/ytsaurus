#pragma once

#include "public.h"
#include "location.h"
#include "session_detail.h"

#include <yt/server/cell_node/public.h>

#include <yt/server/misc/memory_usage_tracker.h>

#include <yt/ytlib/chunk_client/data_node_service_proxy.h>
#include <yt/ytlib/chunk_client/block.h>

#include <yt/core/concurrency/thread_affinity.h>
#include <yt/core/concurrency/throughput_throttler.h>

#include <yt/core/logging/log.h>

#include <yt/core/profiling/profiler.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EBlobSessionSlotState,
    (Empty)
    (Received)
    (Written)
);

class TBlobSession
    : public TSessionBase
{
public:
    using TSessionBase::TSessionBase;

    NChunkClient::NProto::TChunkInfo GetChunkInfo() const override;

private:
    using ESlotState = EBlobSessionSlotState;

    struct TSlot
    {
        ESlotState State = ESlotState::Empty;
        NChunkClient::TBlock Block;
        TPromise<void> WrittenPromise = NewPromise<void>();
        NCellNode::TNodeMemoryTrackerGuard MemoryTrackerGuard;
        TPendingIOGuard PendingIOGuard;
    };

    // Thread affinity: WriterThread
    TError Error_;
    NChunkClient::TFileWriterPtr Writer_;

    // Thread affinity: ControlThread
    std::vector<TSlot> Window_;
    int WindowStartBlockIndex_ = 0;
    int WindowIndex_ = 0;
    i64 Size_ = 0;
    int BlockCount_ = 0;


    virtual TFuture<void> DoStart() override;
    void DoOpenWriter();
   
    virtual TFuture<void> DoPutBlocks(
        int startBlockIndex,
        const std::vector<NChunkClient::TBlock>& blocks,
        bool enableCaching) override;

    virtual TFuture<void> DoSendBlocks(
        int startBlockIndex,
        int blockCount,
        const NNodeTrackerClient::TNodeDescriptor& targetDescriptor) override;

    virtual TFuture<void> DoFlushBlocks(int blockIndex) override;

    virtual void DoCancel() override;

    virtual TFuture<IChunkPtr> DoFinish(
        const NChunkClient::NProto::TChunkMeta* chunkMeta,
        const TNullable<int>& blockCount) override;

    bool IsInWindow(int blockIndex);
    void ValidateBlockIsInWindow(int blockIndex);
    TSlot& GetSlot(int blockIndex);
    void ReleaseBlocks(int flushedBlockIndex);
    NChunkClient::TBlock GetBlock(int blockIndex);
    void MarkAllSlotsWritten(const TError& error);

    TFuture<void> AbortWriter();
    void DoAbortWriter();
    void OnWriterAborted(const TError& error);

    TFuture<void> CloseWriter(const NChunkClient::NProto::TChunkMeta& chunkMeta);
    void DoCloseWriter(const NChunkClient::NProto::TChunkMeta& chunkMeta);
    IChunkPtr OnWriterClosed(const TError& error);

    void DoWriteBlock(const NChunkClient::TBlock& block, int blockIndex);
    void OnBlockWritten(int blockIndex, const TError& error);

    void OnBlockFlushed(int blockIndex, const TError& error);

    void ReleaseSpace();

    void SetFailed(const TError& error);

};

DEFINE_REFCOUNTED_TYPE(TBlobSession)

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT

