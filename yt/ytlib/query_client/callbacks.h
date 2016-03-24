#pragma once

#include "public.h"
#include "plan_fragment_common.h"

#include <yt/ytlib/ypath/public.h>

#include <yt/core/rpc/public.h>

#include <yt/core/actions/future.h>

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

using TExecuteQuery = std::function<TFuture<TQueryStatistics>(
    const TQueryPtr& query,
    TGuid dataId,
    TRowBufferPtr buffer,
    TRowRanges ranges,
    ISchemafulWriterPtr writer)>;

////////////////////////////////////////////////////////////////////////////////

struct IExecutor
    : public virtual TRefCounted
{
    virtual TFuture<TQueryStatistics> Execute(
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        TDataRanges dataSource,
        ISchemafulWriterPtr writer,
        const TQueryOptions& options) = 0;

};

DEFINE_REFCOUNTED_TYPE(IExecutor)

struct ISubExecutor
    : public virtual TRefCounted
{
    virtual TFuture<TQueryStatistics> Execute(
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        std::vector<TDataRanges> dataSources,
        ISchemafulWriterPtr writer,
        const TQueryOptions& options) = 0;

};

DEFINE_REFCOUNTED_TYPE(ISubExecutor)

////////////////////////////////////////////////////////////////////////////////

struct IPrepareCallbacks
{
    virtual ~IPrepareCallbacks()
    { }

    //! Returns an initial split for a given path.
    virtual TFuture<TDataSplit> GetInitialSplit(
        const NYPath::TRichYPath& path,
        TTimestamp timestamp) = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

