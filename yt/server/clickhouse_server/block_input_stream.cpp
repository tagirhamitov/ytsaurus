#include "block_input_stream.h"

#include "type_translation.h"
#include "helpers.h"
#include "db_helpers.h"

#include <yt/client/table_client/schemaless_reader.h>
#include <yt/client/table_client/name_table.h>

#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypeNullable.h>

#include <Columns/ColumnNullable.h>
#include <Columns/ColumnsNumber.h>

namespace NYT::NClickHouseServer {

using namespace NTableClient;
using namespace NLogging;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TBlockInputStream
    : public DB::IBlockInputStream
{
public:
    TBlockInputStream(ISchemalessReaderPtr reader, TTableSchema readSchema, TLogger logger)
        : Reader_(std::move(reader))
        , ReadSchema_(std::move(readSchema))
        , Logger(std::move(logger))
    {
        PrepareHeader();
    }

    virtual std::string getName() const override
    {
        return "BlockInputStream";
    }

    virtual DB::Block getHeader() const override
    {
        return HeaderBlock_;
    }

    virtual void readPrefixImpl() override
    {
        YT_LOG_DEBUG("readPrefixImpl() is called");
    }

    virtual void readSuffixImpl() override
    {
        YT_LOG_DEBUG("readSuffixImpl() is called");
    }

private:
    ISchemalessReaderPtr Reader_;
    TTableSchema ReadSchema_;
    TLogger Logger;
    DB::Block HeaderBlock_;
    std::vector<int> IdToColumnIndex_;

    DB::Block readImpl() override
    {
        YT_LOG_DEBUG("readImpl() is called");

        auto block = HeaderBlock_.cloneEmpty();

        // TODO(max42): consult with psushin@ about contract here.
        std::vector<TUnversionedRow> rows;
        // TODO(max42): make customizable.
        constexpr int rowsPerRead = 10 * 1024;
        rows.reserve(rowsPerRead);
        while (true) {
            if (!Reader_->Read(&rows)) {
                return {};
            } else if (rows.empty()) {
                YT_LOG_DEBUG("Waiting for ready event");
                WaitFor(Reader_->GetReadyEvent())
                    .ThrowOnError();
                YT_LOG_DEBUG("Ready event happened");
            } else {
                break;
            }
        }
        YT_LOG_DEBUG("Rows are ready");

        for (const auto& row : rows) {
            for (int index = 0; index < static_cast<int>(row.GetCount()); ++index) {
                const auto& value = row[index];
                auto id = value.Id;
                int columnIndex = (id < IdToColumnIndex_.size()) ? IdToColumnIndex_[id] : -1;
                Y_VERIFY(columnIndex != -1);
                switch (value.Type) {
                    case EValueType::Null:
                        // TODO(max42): consider transforming to Y_ASSERT.
                        Y_VERIFY(!ReadSchema_.Columns()[columnIndex].Required());
                        block.getByPosition(columnIndex).column->assumeMutable()->insertDefault();
                        break;

                    // NB(max42): When rewriting this properly, remember that Int64 may
                    // correspond to shorter integer columns.
                    case EValueType::String:
                    case EValueType::Any:
                    case EValueType::Int64:
                    case EValueType::Uint64:
                    case EValueType::Double:
                    case EValueType::Boolean: {
                        auto field = ConvertToField(value);
                        block.getByPosition(columnIndex).column->assumeMutable()->insert(field);
                        break;
                    }
                    default:
                        Y_UNREACHABLE();
                }
            }
        }

        YT_LOG_DEBUG("Block is ready");

        return block;
    }

    void PrepareHeader()
    {
        const auto& dataTypes = DB::DataTypeFactory::instance();

        for (int index = 0; index < static_cast<int>(ReadSchema_.Columns().size()); ++index) {
            const auto& columnSchema = ReadSchema_.Columns()[index];
            auto type = RepresentYtType(columnSchema.GetPhysicalType());
            auto dataType = dataTypes.get(GetTypeName(type));
            auto column = dataType->createColumn();
            if (!columnSchema.Required()) {
                column = DB::ColumnNullable::create(std::move(column), DB::ColumnVector<UInt8>::create());
                dataType = DB::makeNullable(dataType);
            }
            HeaderBlock_.insert({ std::move(column), dataType, columnSchema.Name() });
            auto id = Reader_->GetNameTable()->GetIdOrRegisterName(columnSchema.Name());
            if (static_cast<int>(IdToColumnIndex_.size()) <= id) {
                IdToColumnIndex_.resize(id + 1, -1);
            }
            IdToColumnIndex_[id] = index;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

DB::BlockInputStreamPtr CreateBlockInputStream(
    ISchemalessReaderPtr reader,
    TTableSchema readSchema,
    TLogger logger)
{
    return std::make_shared<TBlockInputStream>(std::move(reader), std::move(readSchema), logger);
}

////////////////////////////////////////////////////////////////////////////////

class TBlockInputStreamLoggingAdapter
    : public DB::IBlockInputStream
{
public:
    TBlockInputStreamLoggingAdapter(DB::BlockInputStreamPtr stream, TLogger logger)
        : UnderlyingStream_(std::move(stream))
        , Logger(logger)
    {
        Logger.AddTag("UnderlyingStream: %v", static_cast<void*>(UnderlyingStream_.get()));
        YT_LOG_DEBUG("Stream created");
        addChild(UnderlyingStream_);
    }

    virtual void readPrefix() override
    {
        YT_LOG_DEBUG("readPrefix() is called");
        UnderlyingStream_->readPrefix();
    }

    virtual void readSuffix() override
    {
        YT_LOG_DEBUG("readSuffix() is called");
        UnderlyingStream_->readSuffix();
    }

    virtual DB::Block readImpl() override
    {
        YT_LOG_DEBUG("Started reading from the underlying stream");
        auto result = UnderlyingStream_->read();
        YT_LOG_DEBUG("Finished reading from the underlying stream");
        return result;
    }

    virtual DB::String getName() const override
    {
        return "TBlockInputStreamLoggingAdapter";
    }

    virtual DB::Block getHeader() const override
    {
        YT_LOG_DEBUG("Started getting header from the underlying stream");
        auto result = UnderlyingStream_->getHeader();
        YT_LOG_DEBUG("Finished getting header from the underlying stream");
        return result;
    }

    virtual const DB::BlockMissingValues& getMissingValues() const override
    {
        return UnderlyingStream_->getMissingValues();
    }

    virtual bool isSortedOutput() const override
    {
        return UnderlyingStream_->isSortedOutput();
    }

    virtual const DB::SortDescription& getSortDescription() const override
    {
        return UnderlyingStream_->getSortDescription();
    }

    virtual DB::Block getTotals() override
    {
        return UnderlyingStream_->getTotals();
    }

    virtual void progress(const DB::Progress& value) override
    {
        UnderlyingStream_->progress(value);
    }

    virtual void cancel(bool kill) override
    {
        UnderlyingStream_->cancel(kill);
    }

private:
    DB::BlockInputStreamPtr UnderlyingStream_;
    TLogger Logger;
};

////////////////////////////////////////////////////////////////////////////////

DB::BlockInputStreamPtr CreateBlockInputStreamLoggingAdapter(DB::BlockInputStreamPtr stream, TLogger logger)
{
    return std::make_shared<TBlockInputStreamLoggingAdapter>(std::move(stream), logger);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
