#include "versioned_chunk_writer.h"
#include "private.h"
#include "chunk_meta_extensions.h"
#include "config.h"
#include "unversioned_row.h"
#include "versioned_block_writer.h"
#include "versioned_writer.h"
#include "row_merger.h"
#include "row_buffer.h"

#include <yt/ytlib/api/native_client.h>
#include <yt/ytlib/api/native_connection.h>

#include <yt/ytlib/table_chunk_format/column_writer.h>
#include <yt/ytlib/table_chunk_format/data_block_writer.h>
#include <yt/ytlib/table_chunk_format/timestamp_writer.h>

#include <yt/ytlib/chunk_client/chunk_spec.h>
#include <yt/ytlib/chunk_client/chunk_writer.h>
#include <yt/ytlib/chunk_client/dispatcher.h>
#include <yt/ytlib/chunk_client/encoding_chunk_writer.h>
#include <yt/ytlib/chunk_client/encoding_writer.h>
#include <yt/ytlib/chunk_client/multi_chunk_writer_base.h>

#include <yt/core/misc/range.h>
#include <yt/core/misc/random.h>

namespace NYT {
namespace NTableClient {

using namespace NTableChunkFormat;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;
using namespace NRpc;
using namespace NTransactionClient;
using namespace NObjectClient;
using namespace NApi;
using namespace NTableClient::NProto;

////////////////////////////////////////////////////////////////////////////////

const i64 MinRowRangeDataWeight = (i64) 64 * 1024;

////////////////////////////////////////////////////////////////////////////////

class TVersionedChunkWriterBase
    : public IVersionedChunkWriter
{
public:
    TVersionedChunkWriterBase(
        TChunkWriterConfigPtr config,
        TChunkWriterOptionsPtr options,
        const TTableSchema& schema,
        IChunkWriterPtr chunkWriter,
        IBlockCachePtr blockCache)
        : Config_(config)
        , Schema_(schema)
        , EncodingChunkWriter_(New<TEncodingChunkWriter>(
           std::move(config),
           std::move(options),
           std::move(chunkWriter),
           std::move(blockCache),
           Logger))
    , LastKey_(static_cast<TUnversionedValue*>(nullptr), static_cast<TUnversionedValue*>(nullptr))
    , MinTimestamp_(MaxTimestamp)
    , MaxTimestamp_(MinTimestamp)
    , RandomGenerator_(RandomNumber<ui64>())
    , SamplingThreshold_(static_cast<ui64>(std::numeric_limits<ui64>::max() * Config_->SampleRate))
    , SamplingRowMerger_(New<TSamplingRowMerger>(
        New<TRowBuffer>(TVersionedChunkWriterBaseTag()),
        schema))
#if 0
    , KeyFilter_(Config_->MaxKeyFilterSize, Config_->KeyFilterFalsePositiveRate)
#endif
    { }

    virtual TFuture<void> Open() override
    {
        return VoidFuture;
    }

    virtual TFuture<void> GetReadyEvent() override
    {
        return EncodingChunkWriter_->GetReadyEvent();
    }

    virtual i64 GetRowCount() const override
    {
        return RowCount_;
    }

    virtual bool Write(const std::vector<TVersionedRow>& rows) override
    {
        YCHECK(rows.size() > 0);

        SamplingRowMerger_->Reset();

        if (RowCount_ == 0) {
            ToProto(
                BoundaryKeysExt_.mutable_min(),
                TOwningKey(rows.front().BeginKeys(), rows.front().EndKeys()));
            EmitSample(rows.front());
        }

        DoWriteRows(rows);

        LastKey_ = TOwningKey(rows.back().BeginKeys(), rows.back().EndKeys());
        return EncodingChunkWriter_->IsReady();
    }

    virtual TFuture<void> Close() override
    {
        // psushin@ forbids empty chunks :)
        YCHECK(RowCount_ > 0);

        return BIND(&TVersionedChunkWriterBase::DoClose, MakeStrong(this))
            .AsyncVia(NChunkClient::TDispatcher::Get()->GetWriterInvoker())
            .Run();
    }

    virtual i64 GetMetaSize() const override
    {
        // Other meta parts are negligible.
        return BlockMetaExtSize_ + SamplesExtSize_;
    }

    virtual bool IsCloseDemanded() const override
    {
        return false;
    }

    virtual TChunkMeta GetSchedulerMeta() const override
    {
        return GetMasterMeta();
    }

    virtual TChunkMeta GetNodeMeta() const override
    {
        return EncodingChunkWriter_->Meta();
    }

    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const override
    {
        return EncodingChunkWriter_->GetDataStatistics();
    }

protected:
    NLogging::TLogger Logger = TableClientLogger;

    TChunkWriterConfigPtr Config_;
    TTableSchema Schema_;

    TEncodingChunkWriterPtr EncodingChunkWriter_;

    TOwningKey LastKey_;

    TBlockMetaExt BlockMetaExt_;
    i64 BlockMetaExtSize_ = 0;

    TSamplesExt SamplesExt_;
    i64 SamplesExtSize_ = 0;

    i64 DataWeight_ = 0;

    TBoundaryKeysExt BoundaryKeysExt_;

    i64 RowCount_ = 0;

    TTimestamp MinTimestamp_;
    TTimestamp MaxTimestamp_;

    TRandomGenerator RandomGenerator_;
    const ui64 SamplingThreshold_;

    struct TVersionedChunkWriterBaseTag { };
    TSamplingRowMergerPtr SamplingRowMerger_;

#if 0
    TBloomFilterBuilder KeyFilter_;
#endif

    virtual void DoClose() = 0;
    virtual void DoWriteRows(const std::vector<TVersionedRow>& rows) = 0;

    void EmitSampleRandomly(TVersionedRow row)
    {
        if (RandomGenerator_.Generate<ui64>() < SamplingThreshold_) {
            EmitSample(row);
        }
    }

    void EmitSample(TVersionedRow row)
    {
        auto mergedRow = SamplingRowMerger_->MergeRow(row);
        ToProto(SamplesExt_.add_entries(), mergedRow);
        SamplesExtSize_ += SamplesExt_.entries(SamplesExt_.entries_size() - 1).length();
    }

    void ValidateRow(
        TVersionedRow row,
        const TUnversionedValue* beginPrevKey,
        const TUnversionedValue* endPrevKey) const
    {
        YCHECK(
            !beginPrevKey && !endPrevKey ||
            CompareRows(beginPrevKey, endPrevKey, row.BeginKeys(), row.EndKeys()) < 0);
        YCHECK(row.GetWriteTimestampCount() <= std::numeric_limits<ui16>::max());
        YCHECK(row.GetDeleteTimestampCount() <= std::numeric_limits<ui16>::max());
    }
};

////////////////////////////////////////////////////////////////////////////////

class TSimpleVersionedChunkWriter
    : public TVersionedChunkWriterBase
{
public:
    TSimpleVersionedChunkWriter(
        TChunkWriterConfigPtr config,
        TChunkWriterOptionsPtr options,
        const TTableSchema& schema,
        IChunkWriterPtr chunkWriter,
        IBlockCachePtr blockCache)
        : TVersionedChunkWriterBase(
            std::move(config),
            std::move(options),
            schema,
            std::move(chunkWriter),
            std::move(blockCache))
        , BlockWriter_(new TSimpleVersionedBlockWriter(Schema_))
    {
        Logger.AddTag("SimpleVersionedChunkWriter: %p", this);
    }

    virtual i64 GetDataSize() const override
    {
        return EncodingChunkWriter_->GetDataStatistics().compressed_data_size() +
               (BlockWriter_ ? BlockWriter_->GetBlockSize() : 0);
    }

    virtual TChunkMeta GetMasterMeta() const override
    {
        TChunkMeta meta;
        FillCommonMeta(&meta);
        SetProtoExtension(meta.mutable_extensions(), EncodingChunkWriter_->MiscExt());
        return meta;
    }

private:
    std::unique_ptr<TSimpleVersionedBlockWriter> BlockWriter_;

    virtual void DoWriteRows(const std::vector<TVersionedRow>& rows) override
    {
        //FIXME: insert key into bloom filter.
        //KeyFilter_.Insert(GetFarmFingerprint(rows.front().BeginKeys(), rows.front().EndKeys()));
        ValidateRow(rows.front(), LastKey_.Begin(), LastKey_.End());
        WriteRow(rows.front(), LastKey_.Begin(), LastKey_.End());
        FinishBlockIfLarge(rows.front());

        for (int i = 1; i < rows.size(); ++i) {
            //KeyFilter_.Insert(GetFarmFingerprint(rows[i].BeginKeys(), rows[i].EndKeys()));
            ValidateRow(rows.front(), LastKey_.Begin(), LastKey_.End());
            WriteRow(rows[i], rows[i - 1].BeginKeys(), rows[i - 1].EndKeys());
            FinishBlockIfLarge(rows[i]);
        }
    }

    void WriteRow(
        TVersionedRow row,
        const TUnversionedValue* beginPreviousKey,
        const TUnversionedValue* endPreviousKey)
    {
        EmitSampleRandomly(row);

        ++RowCount_;
        DataWeight_ += GetDataWeight(row);
        BlockWriter_->WriteRow(row, beginPreviousKey, endPreviousKey);
    }

    void FinishBlockIfLarge(TVersionedRow row)
    {
        if (BlockWriter_->GetBlockSize() < Config_->BlockSize) {
            return;
        }

        FinishBlock(row.BeginKeys(), row.EndKeys());
        BlockWriter_.reset(new TSimpleVersionedBlockWriter(Schema_));
    }

    void FinishBlock(const TUnversionedValue* beginKey, const TUnversionedValue* endKey)
    {
        auto block = BlockWriter_->FlushBlock();
        block.Meta.set_chunk_row_count(RowCount_);
        block.Meta.set_block_index(BlockMetaExt_.blocks_size());
        ToProto(block.Meta.mutable_last_key(), beginKey, endKey);

        BlockMetaExtSize_ += block.Meta.ByteSize();

        BlockMetaExt_.add_blocks()->Swap(&block.Meta);
        EncodingChunkWriter_->WriteBlock(std::move(block.Data));

        MaxTimestamp_ = std::max(MaxTimestamp_, BlockWriter_->GetMaxTimestamp());
        MinTimestamp_ = std::min(MinTimestamp_, BlockWriter_->GetMinTimestamp());
    }

    virtual void DoClose() override
    {
        using NYT::ToProto;

        if (BlockWriter_->GetRowCount() > 0) {
            FinishBlock(LastKey_.Begin(), LastKey_.End());
        }

        ToProto(BoundaryKeysExt_.mutable_max(), LastKey_);

        auto& meta = EncodingChunkWriter_->Meta();
        FillCommonMeta(&meta);

        SetProtoExtension(meta.mutable_extensions(), ToProto<TTableSchemaExt>(Schema_));

        SetProtoExtension(meta.mutable_extensions(), BlockMetaExt_);
        SetProtoExtension(meta.mutable_extensions(), SamplesExt_);

#if 0
        if (KeyFilter_.IsValid()) {
        KeyFilter_.Shrink();
        //FIXME: write bloom filter to chunk.
    }
#endif

        auto& miscExt = EncodingChunkWriter_->MiscExt();
        miscExt.set_sorted(true);
        miscExt.set_row_count(RowCount_);
        miscExt.set_data_weight(DataWeight_);
        miscExt.set_min_timestamp(MinTimestamp_);
        miscExt.set_max_timestamp(MaxTimestamp_);

        EncodingChunkWriter_->Close();
    }

    void FillCommonMeta(TChunkMeta* meta) const
    {
        meta->set_type(static_cast<int>(EChunkType::Table));
        meta->set_version(static_cast<int>(TSimpleVersionedBlockWriter::FormatVersion));

        SetProtoExtension(meta->mutable_extensions(), BoundaryKeysExt_);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TColumnVersionedChunkWriter
    : public TVersionedChunkWriterBase
{
public:
    TColumnVersionedChunkWriter(
        TChunkWriterConfigPtr config,
        TChunkWriterOptionsPtr options,
        const TTableSchema& schema,
        IChunkWriterPtr chunkWriter,
        IBlockCachePtr blockCache)
        : TVersionedChunkWriterBase(
            std::move(config),
            std::move(options),
            schema,
            std::move(chunkWriter),
            std::move(blockCache))
        , DataToBlockFlush_(Config_->BlockSize)
    {
        Logger.AddTag("ColumnVersionedChunkWriter: %p", this);

        // Only scan-optimized version for now.
        yhash_map<Stroka, TDataBlockWriter*> groupBlockWriters;
        for (const auto& column : Schema_.Columns()) {
            if (column.Group && groupBlockWriters.find(*column.Group) == groupBlockWriters.end()) {
                auto blockWriter = std::make_unique<TDataBlockWriter>();
                groupBlockWriters[*column.Group] = blockWriter.get();
                BlockWriters_.emplace_back(std::move(blockWriter));
            }
        }

        auto getBlockWriter = [&] (const NTableClient::TColumnSchema& columnSchema) -> TDataBlockWriter* {
            if (columnSchema.Group) {
                return groupBlockWriters[*columnSchema.Group];
            } else {
                BlockWriters_.emplace_back(std::make_unique<TDataBlockWriter>());
                return BlockWriters_.back().get();
            }
        };

        // Key columns.
        for (int keyColumnIndex = 0; keyColumnIndex < Schema_.GetKeyColumnCount(); ++keyColumnIndex) {
            const auto& column = Schema_.Columns()[keyColumnIndex];
            ValueColumnWriters_.emplace_back(CreateUnversionedColumnWriter(
                column,
                keyColumnIndex,
                getBlockWriter(column)));
        }

        // Non-key columns.
        for (
            int valueColumnIndex = Schema_.GetKeyColumnCount();
            valueColumnIndex < Schema_.Columns().size();
            ++valueColumnIndex)
        {
            const auto& column = Schema_.Columns()[valueColumnIndex];
            ValueColumnWriters_.emplace_back(CreateVersionedColumnWriter(
                column,
                valueColumnIndex,
                getBlockWriter(column)));
        }

        auto blockWriter = std::make_unique<TDataBlockWriter>();
        TimestampWriter_ = CreateTimestampWriter(blockWriter.get());
        BlockWriters_.emplace_back(std::move(blockWriter));

        YCHECK(BlockWriters_.size() > 1);
    }

    virtual i64 GetDataSize() const override
    {
        i64 result = EncodingChunkWriter_->GetDataStatistics().compressed_data_size();
        for (const auto& blockWriter : BlockWriters_) {
            result += blockWriter->GetCurrentSize();
        }
        return result;
    }

    virtual TChunkMeta GetMasterMeta() const override
    {
        TChunkMeta meta;
        FillCommonMeta(&meta);
        SetProtoExtension(meta.mutable_extensions(), EncodingChunkWriter_->MiscExt());
        return meta;
    }

private:
    std::vector<std::unique_ptr<TDataBlockWriter>> BlockWriters_;
    std::vector<std::unique_ptr<IValueColumnWriter>> ValueColumnWriters_;
    std::unique_ptr<ITimestampWriter> TimestampWriter_;

    i64 DataToBlockFlush_;

    virtual void DoWriteRows(const std::vector<TVersionedRow>& rows) override
    {
        auto* data = const_cast<TVersionedRow*>(rows.data());

        int startRowIndex = 0;
        while (startRowIndex < rows.size()) {
            i64 weight = 0;
            int rowIndex = startRowIndex;
            for (; rowIndex < rows.size() && weight < DataToBlockFlush_; ++rowIndex) {
                if (rowIndex == 0) {
                    ValidateRow(rows[rowIndex], LastKey_.Begin(), LastKey_.End());
                } else {
                    ValidateRow(
                        rows[rowIndex],
                        rows[rowIndex - 1].BeginKeys(),
                        rows[rowIndex - 1].EndKeys());
                }

                ValidateRow(rows[rowIndex], LastKey_.Begin(), LastKey_.End());
                weight += GetDataWeight(rows[rowIndex]);
            }

            auto range = MakeRange(data + startRowIndex, data + rowIndex);
            for (auto& columnWriter : ValueColumnWriters_) {
                columnWriter->WriteValues(range);
            }
            TimestampWriter_->WriteTimestamps(range);

            RowCount_ += range.Size();
            DataWeight_ += weight;

            startRowIndex = rowIndex;

            TryFlushBlock(rows[rowIndex - 1]);
        }

        for (auto row : rows) {
            EmitSampleRandomly(row);
        }
    }

    void TryFlushBlock(TVersionedRow lastRow)
    {
        while (true) {
            i64 totalSize = 0;
            i64 maxWriterSize = -1;
            int maxWriterIndex = -1;

            for (int i = 0; i < BlockWriters_.size(); ++i) {
                auto size = BlockWriters_[i]->GetCurrentSize();
                totalSize += size;
                if (size > maxWriterSize) {
                    maxWriterIndex = i;
                    maxWriterSize = size;
                }
            }

            YCHECK(maxWriterIndex >= 0);

            if (totalSize > Config_->MaxBufferSize || maxWriterSize > Config_->BlockSize) {
                FinishBlock(maxWriterIndex, lastRow.BeginKeys(), lastRow.EndKeys());
            } else {
                DataToBlockFlush_ = std::min(Config_->MaxBufferSize - totalSize, Config_->BlockSize - maxWriterSize);
                DataToBlockFlush_ = std::max(MinRowRangeDataWeight, DataToBlockFlush_);

                break;
            }
        }
    }

    void FinishBlock(int blockWriterIndex, const TUnversionedValue* beginKey, const TUnversionedValue* endKey)
    {
        auto block = BlockWriters_[blockWriterIndex]->DumpBlock(BlockMetaExt_.blocks_size(), RowCount_);

        block.Meta.set_block_index(BlockMetaExt_.blocks_size());
        ToProto(block.Meta.mutable_last_key(), beginKey, endKey);

        BlockMetaExtSize_ += block.Meta.ByteSize();

        BlockMetaExt_.add_blocks()->Swap(&block.Meta);
        EncodingChunkWriter_->WriteBlock(std::move(block.Data));
    }

    virtual void DoClose() override
    {
        using NYT::ToProto;

        for (int i = 0; i < BlockWriters_.size(); ++i) {
            if (BlockWriters_[i]->GetCurrentSize() > 0) {
                FinishBlock(i, LastKey_.Begin(), LastKey_.End());
            }
        }

        ToProto(BoundaryKeysExt_.mutable_max(), LastKey_);

        auto& meta = EncodingChunkWriter_->Meta();
        FillCommonMeta(&meta);

        SetProtoExtension(meta.mutable_extensions(), ToProto<TTableSchemaExt>(Schema_));
        SetProtoExtension(meta.mutable_extensions(), BlockMetaExt_);
        SetProtoExtension(meta.mutable_extensions(), SamplesExt_);

        NProto::TColumnMetaExt columnMetaExt;
        for (const auto& valueColumnWriter : ValueColumnWriters_) {
            *columnMetaExt.add_columns() = valueColumnWriter->ColumnMeta();
        }
        *columnMetaExt.add_columns() = TimestampWriter_->ColumnMeta();

        SetProtoExtension(meta.mutable_extensions(), columnMetaExt);

#if 0
        if (KeyFilter_.IsValid()) {
        KeyFilter_.Shrink();
        //FIXME: write bloom filter to chunk.
    }
#endif

        auto& miscExt = EncodingChunkWriter_->MiscExt();
        miscExt.set_sorted(true);
        miscExt.set_row_count(RowCount_);
        miscExt.set_data_weight(DataWeight_);
        miscExt.set_min_timestamp(static_cast<i64>(TimestampWriter_->GetMinTimestamp()));
        miscExt.set_max_timestamp(static_cast<i64>(TimestampWriter_->GetMaxTimestamp()));

        EncodingChunkWriter_->Close();
    }

    void FillCommonMeta(TChunkMeta* meta) const
    {
        meta->set_type(static_cast<int>(EChunkType::Table));
        meta->set_version(static_cast<int>(ETableChunkFormat::VersionedColumnar));

        SetProtoExtension(meta->mutable_extensions(), BoundaryKeysExt_);
    }
};

////////////////////////////////////////////////////////////////////////////////

IVersionedChunkWriterPtr CreateVersionedChunkWriter(
    TChunkWriterConfigPtr config,
    TChunkWriterOptionsPtr options,
    const TTableSchema& schema,
    IChunkWriterPtr chunkWriter,
    IBlockCachePtr blockCache)
{
    if (options->OptimizeFor == EOptimizeFor::Scan) {
        return New<TColumnVersionedChunkWriter>(
            std::move(config),
            std::move(options),
            schema,
            std::move(chunkWriter),
            std::move(blockCache));
    } else {
        return New<TSimpleVersionedChunkWriter>(
            std::move(config),
            std::move(options),
            schema,
            std::move(chunkWriter),
            std::move(blockCache));
    }
}

////////////////////////////////////////////////////////////////////////////////

IVersionedMultiChunkWriterPtr CreateVersionedMultiChunkWriter(
    TTableWriterConfigPtr config,
    TTableWriterOptionsPtr options,
    const TTableSchema& schema,
    INativeClientPtr client,
    TCellTag cellTag,
    const NTransactionClient::TTransactionId& transactionId,
    const TChunkListId& parentChunkListId,
    IThroughputThrottlerPtr throttler,
    IBlockCachePtr blockCache)
{
    typedef TMultiChunkWriterBase<
        IVersionedMultiChunkWriter,
        IVersionedChunkWriter,
        const std::vector<TVersionedRow>&> TVersionedMultiChunkWriter;

    auto createChunkWriter = [=] (IChunkWriterPtr underlyingWriter) {
        return CreateVersionedChunkWriter(
            config,
            options,
            schema,
            underlyingWriter,
            blockCache);
    };

    return New<TVersionedMultiChunkWriter>(
        config,
        options,
        client,
        cellTag,
        transactionId,
        parentChunkListId,
        createChunkWriter,
        throttler,
        blockCache);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
