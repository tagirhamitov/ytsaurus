#pragma once

///
/// @file mapreduce/yt/interface/io.h
///
/// Header containing client interface for reading and writing tables and files.


#include "fwd.h"

#include "client_method_options.h"
#include "common.h"
#include "format.h"
#include "node.h"
#include "mpl.h"

#include <contrib/libs/protobuf/message.h>

#include <statbox/ydl/runtime/cpp/gen_support/traits.h>

#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/generic/yexception.h>
#include <util/generic/maybe.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

/// "Marker" type to use for YDL types in @ref NYT::TTableWriter.
class TYdlGenericRowType
{ };

} // namespace NDetail

/// @brief "Marker" type to use for YDL types in @ref NYT::TTableReader.
///
/// @tparam TYdlRowTypes Possible types of rows to be read.
template<class ... TYdlRowTypes>
class TYdlOneOf
{
    static_assert((NYdl::TIsYdlGenerated<TYdlRowTypes>::value && ...), "Template parameters can only be YDL types");
};

////////////////////////////////////////////////////////////////////////////////

struct INodeReaderImpl;
struct IYaMRReaderImpl;
struct IProtoReaderImpl;
struct IYdlReaderImpl;
struct INodeWriterImpl;
struct IYaMRWriterImpl;
struct IYdlWriterImpl;
struct IProtoWriterImpl;

////////////////////////////////////////////////////////////////////////////////

/// Class of exceptions connected to reading or writing tables or files.
class TIOException
    : public yexception
{ };

///////////////////////////////////////////////////////////////////////////////

/// Interface representing YT file reader.
class IFileReader
    : public TThrRefBase
    , public IInputStream
{ };

/// Interface representing YT file writer.
class IFileWriter
    : public TThrRefBase
    , public IOutputStream
{ };

////////////////////////////////////////////////////////////////////////////////

/// Low-level interface to read YT table with retries.
class TRawTableReader
    : public TThrRefBase
    , public IInputStream
{
public:
    /// @brief Retry table read starting from the specified `rangeIndex` and `rowIndex`.
    ///
    /// @param rangeIndex Index of first range to read
    /// @param rowIndex Index of first row to read; if `rowIndex == Nothing` entire request will be retried.
    ///
    /// @return `true` on successful request retry, `false` if no retry attempts are left (then `Retry()` shouldn't be called any more).
    ///
    /// `rowIndex` must be inside the range with index `rangeIndex` if the latter is specified.
    ///
    /// After successful retry the user should reset `rangeIndex` / `rowIndex` values and read new ones
    /// from the stream.
    virtual bool Retry(
        const TMaybe<ui32>& rangeIndex,
        const TMaybe<ui64>& rowIndex) = 0;

    /// Resets retry attempt count to the initial value (then `Retry()` can be called again).
    virtual void ResetRetries() = 0;

    /// @brief May the input stream contain table ranges?
    ///
    /// In the case when it is `true` the `TRawTableReader` user is responsible
    /// to track active range index in order to pass it to Retry().
    virtual bool HasRangeIndices() const = 0;
};

/// @brief Low-level interface to write YT table.
///
/// Retries must be handled by implementation.
class TRawTableWriter
    : public TThrRefBase
    , public IOutputStream
{
public:
    /// @brief Call this method after complete row representation is written to the stream.
    ///
    /// When this method is called `TRowTableWriter` can check its buffer
    /// and if it is full send data to YT.
    /// @note `TRawTableWriter` never sends partial records to YT (due to retries).
    virtual void NotifyRowEnd() = 0;

    /// @brief Try to abort writing process as soon as possible (makes sense for multi-threaded writers).
    ///
    /// By default it does nothing, but implementations are welcome to override this method.
    virtual void Abort()
    { }
};

////////////////////////////////////////////////////////////////////////////////

/// @brief Class template to read typed rows from YT tables.
///
/// @tparam T Row type.
///
/// Correct usage of this class usually looks like
/// ```
/// for (const auto& cursor : *reader) {
///     const auto& row = cursor.GetRow();
///     ...
/// }
/// ```
/// or, more verbosely,
/// ```
/// for (; reader->IsValid(); reader->Next()) {
///     const auto& row = reader->GetRow();
///     ...
/// }
/// ```
///
/// @note Actual (partial) specializations of this template may look a bit different,
/// e.g. @ref NYT::TTableReader::GetRow, @ref NYT::TTableReader::MoveRow may be method templates.
template <class T, class>
class TTableReader
    : public TThrRefBase
{
public:
    /// Get current row.
    const T& GetRow() const;

    /// Extract current row; further calls to `GetRow` and `MoveRow` will fail.
    T MoveRow();

    /// Extract current row to `result`; further calls to `GetRow` and `MoveRow` will fail.
    void MoveRow(T* result);

    /// Check whether all the rows were read.
    bool IsValid() const;

    /// Move the cursor to the next row. 
    void Next();

    /// Get table index of the current row.
    ui32 GetTableIndex() const;

    /// Get range index of the current row (zero if it is unknown or read request contains no ranges)
    ui32 GetRangeIndex() const;

    /// Get current row index (zero if it unknown).
    ui64 GetRowIndex() const;
};

/// @brief Iterator for use in range-based-for.
///
/// @note Idiomatic usage:
/// ```
/// for (const auto& cursor : *reader) {
///     const auto& row = cursor.GetRow();
///     ...
/// }
/// ```
template <class T>
class TTableReaderIterator
{
public:
    /// Construct iterator from table reader (can be `nullptr`).
    explicit TTableReaderIterator<T>(TTableReader<T>* reader)
    {
        if (reader && reader->IsValid()) {
            Reader_ = reader;
        } else {
            Reader_ = nullptr;
        }
    }

    /// Equality operator.
    bool operator==(const TTableReaderIterator& it) const
    {
        return Reader_ == it.Reader_;
    }

    /// Inequality operator.
    bool operator!=(const TTableReaderIterator& it) const
    {
        return Reader_ != it.Reader_;
    }

    /// Dereference operator.
    TTableReader<T>& operator*()
    {
        return *Reader_;
    }

    /// Const dereference operator.
    const TTableReader<T>& operator*() const
    {
        return *Reader_;
    }

    /// Preincrement operator.
    TTableReaderIterator& operator++()
    {
        Reader_->Next();
        if (!Reader_->IsValid()) {
            Reader_ = nullptr;
        }
        return *this;
    }

private:
    TTableReader<T>* Reader_;
};

/// @brief Function to facilitate range-based-for for @ref NYT::TTableReader.
///
/// @see @ref NYT::TTableReaderIterator
template <class T>
TTableReaderIterator<T> begin(TTableReader<T>& reader)
{
    return TTableReaderIterator<T>(&reader);
}

/// @brief Function to facilitate range-based-for for @ref NYT::TTableReader.
///
/// @see @ref NYT::TTableReaderIterator
template <class T>
TTableReaderIterator<T> end(TTableReader<T>&)
{
    return TTableReaderIterator<T>(nullptr);
}

////////////////////////////////////////////////////////////////////////////////

/// @brief Class to facilitate reading table rows sorted by key.
///
/// Each reader returned from @ref NYT::TTableRangesReader::GetRange represents
/// a range of rows with the same key.
///
/// @note Idiomatic usage:
/// ```
/// for (; reader->IsValid(); reader->Next()) {
///     auto& rangeReader = reader->GetRange();
///     ...
/// }
/// ```
template <class T, class>
class TTableRangesReader
    : public TThrRefBase
{
public:
    /// Get reader for rows with the same key.
    TTableReader<T>& GetRange();

    /// Check whether all rows are read.
    bool IsValid() const;

    /// Move cursor to the next range.
    void Next();
};

////////////////////////////////////////////////////////////////////////////////

/// Class template to write typed rows to YT tables.
template <class T, class>
class TTableWriter
    : public TThrRefBase
{
public:
    /// @brief Submit a row for writing.
    ///
    /// The row may (and very probably will) *not* be written immediately.
    void AddRow(const T& row);

    /// Stop writing data as soon as possible (without flushing data, e.g. before aborting parent transaction).
    void Finish();
};

////////////////////////////////////////////////////////////////////////////////

/// @brief Type representing YaMR table row.
///
/// @deprecated
struct TYaMRRow
{
    /// Key column.
    TStringBuf Key;

    /// Subkey column.
    TStringBuf SubKey;

    /// Value column.
    TStringBuf Value;
};

////////////////////////////////////////////////////////////////////////////////

/// Interface for creating table and file readers and writer.
class IIOClient
{
public:
    virtual ~IIOClient() = default;

    /// Create a reader for file at `path`.
    virtual IFileReaderPtr CreateFileReader(
        const TRichYPath& path,
        const TFileReaderOptions& options = TFileReaderOptions()) = 0;

    /// Create a writer for file at `path`.
    virtual IFileWriterPtr CreateFileWriter(
        const TRichYPath& path,
        const TFileWriterOptions& options = TFileWriterOptions()) = 0;

    /// Create a typed reader for table at `path`.
    template <class T>
    TTableReaderPtr<T> CreateTableReader(
        const TRichYPath& path,
        const TTableReaderOptions& options = TTableReaderOptions());

    /// Create a typed writer for table at `path`.
    template <class T>
    TTableWriterPtr<T> CreateTableWriter(
        const TRichYPath& path,
        const TTableWriterOptions& options = TTableWriterOptions());

    /// Create a writer to write protobuf messages with specified descriptor.
    virtual TTableWriterPtr<::google::protobuf::Message> CreateTableWriter(
        const TRichYPath& path,
        const ::google::protobuf::Descriptor& descriptor,
        const TTableWriterOptions& options = TTableWriterOptions()) = 0;

    /// Create a reader to read a table using specified format.
    virtual TRawTableReaderPtr CreateRawReader(
        const TRichYPath& path,
        const TFormat& format,
        const TTableReaderOptions& options = TTableReaderOptions()) = 0;

    /// Create a reader to write a table using specified format.
    virtual TRawTableWriterPtr CreateRawWriter(
        const TRichYPath& path,
        const TFormat& format,
        const TTableWriterOptions& options = TTableWriterOptions()) = 0;

    ///
    /// @brief Create a reader for [blob table](https://wiki.yandex-team.ru/yt/userdoc/blob_tables) at `path`.
    ///
    /// @param path Blob table path.
    /// @param blobId Key identifying the blob.
    /// @param options Optional parameters
    ///
    /// Blob table is a table that stores a number of blobs.
    /// Blobs are sliced into parts of the same size (maybe except of last part).
    /// Those parts are stored in the separate rows.
    ///
    /// Blob table have constaints on its schema.
    ///  - There must be columns that identify blob (blob id columns). That columns might be of any type.
    ///  - There must be a column of `int64` type that identify part inside the blob (this column is called `part index`).
    ///  - There must be a column of `string` type that stores actual data (this column is called `data column`).
    virtual IFileReaderPtr CreateBlobTableReader(
        const TYPath& path,
        const TKey& blobId,
        const TBlobTableReaderOptions& options = TBlobTableReaderOptions()) = 0;

private:
    virtual ::TIntrusivePtr<INodeReaderImpl> CreateNodeReader(
        const TRichYPath& path, const TTableReaderOptions& options) = 0;

    virtual ::TIntrusivePtr<IYaMRReaderImpl> CreateYaMRReader(
        const TRichYPath& path, const TTableReaderOptions& options) = 0;

    virtual ::TIntrusivePtr<IProtoReaderImpl> CreateProtoReader(
        const TRichYPath& path,
        const TTableReaderOptions& options,
        const ::google::protobuf::Message* prototype) = 0;

    virtual ::TIntrusivePtr<IYdlReaderImpl> CreateYdlReader(
        const TRichYPath& /*path*/,
        const TTableReaderOptions& /*options*/,
        NTi::TTypePtr /*type*/)
    {
        Y_FAIL("Uimplemented");
    }

    virtual ::TIntrusivePtr<INodeWriterImpl> CreateNodeWriter(
        const TRichYPath& path, const TTableWriterOptions& options) = 0;

    virtual ::TIntrusivePtr<IYaMRWriterImpl> CreateYaMRWriter(
        const TRichYPath& path, const TTableWriterOptions& options) = 0;

    virtual ::TIntrusivePtr<IProtoWriterImpl> CreateProtoWriter(
        const TRichYPath& path,
        const TTableWriterOptions& options,
        const ::google::protobuf::Message* prototype) = 0;

    virtual ::TIntrusivePtr<IYdlWriterImpl> CreateYdlWriter(
        const TRichYPath& /*path*/,
        const TTableWriterOptions& /*options*/,
        NTi::TTypePtr /*type*/)
    {
        Y_FAIL("Uimplemented");
    }
};

////////////////////////////////////////////////////////////////////////////////

/// @brief Create a protobuf table reader from a stream.
///
/// @tparam T Protobuf message type to read (must be inherited from `Message`).
template <typename T>
TTableReaderPtr<T> CreateTableReader(
    IInputStream* stream,
    const TTableReaderOptions& options = TTableReaderOptions());

/// Create a @ref NYT::TNode table reader from a stream.
template <>
TTableReaderPtr<TNode> CreateTableReader<TNode>(
    IInputStream* stream, const TTableReaderOptions& options);

/// Create a @ref NYT::TYaMRRow table reader from a stream.
template <>
TTableReaderPtr<TYaMRRow> CreateTableReader<TYaMRRow>(
    IInputStream* stream, const TTableReaderOptions& options);

namespace NDetail {

/// Create a protobuf table reader from a stream.
::TIntrusivePtr<IProtoReaderImpl> CreateProtoReader(
    IInputStream* stream,
    const TTableReaderOptions& options,
    const ::google::protobuf::Descriptor* descriptor);

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

/// Convert generic protobuf table reader to a concrete one (for certain type `T`).
template <typename T>
TTableReaderPtr<T> CreateConcreteProtobufReader(TTableReader<Message>* reader);

/// Convert generic protobuf table reader to a concrete one (for certain type `T`).
template <typename T>
TTableReaderPtr<T> CreateConcreteProtobufReader(const TTableReaderPtr<Message>& reader);

/// Convert a concrete (for certain type `T`) protobuf table reader to a generic one.
template <typename T>
TTableReaderPtr<Message> CreateGenericProtobufReader(TTableReader<T>* reader);

/// Convert a concrete (for certain type `T`) protobuf table reader to a generic one.
template <typename T>
TTableReaderPtr<Message> CreateGenericProtobufReader(const TTableReaderPtr<T>& reader);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define IO_INL_H_
#include "io-inl.h"
#undef IO_INL_H_

