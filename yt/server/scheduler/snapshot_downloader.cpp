#include "stdafx.h"
#include "snapshot_downloader.h"
#include "scheduler.h"
#include "config.h"

#include <core/concurrency/scheduler.h>

#include <ytlib/scheduler/helpers.h>

#include <ytlib/api/client.h>
#include <ytlib/api/file_reader.h>

#include <server/cell_scheduler/bootstrap.h>

namespace NYT {
namespace NScheduler {

using namespace NApi;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TSnapshotDownloader::TSnapshotDownloader(
    TSchedulerConfigPtr config,
    NCellScheduler::TBootstrap* bootstrap,
    TOperationPtr operation)
    : Config(config)
    , Bootstrap(bootstrap)
    , Operation(operation)
    , Logger(SchedulerLogger)
{
    YCHECK(bootstrap);
    YCHECK(operation);

    Logger.AddTag(Sprintf("OperationId: %s",
        ~ToString(operation->GetId())));
}

void TSnapshotDownloader::Run()
{
    LOG_INFO("Starting downloading snapshot");

    auto client = Bootstrap->GetMasterClient();

    auto snapshotPath = GetSnapshotPath(Operation->GetId());
    auto reader = client->CreateFileReader(
        snapshotPath,
        TFileReaderOptions(),
        Config->SnapshotReader);

    {
        auto result = WaitFor(reader->Open());
        THROW_ERROR_EXCEPTION_IF_FAILED(result);
    }
        
    i64 size = reader->GetSize();

    LOG_INFO("Snapshot reader opened (Size: %" PRId64 ")", size);
    
    Operation->Snapshot() = TBlob();

    try {
        auto& blob = *Operation->Snapshot();
        blob.Reserve(size);

        while (true) {
            auto blockOrError = WaitFor(reader->Read());
            THROW_ERROR_EXCEPTION_IF_FAILED(blockOrError);
            auto block = blockOrError.Value();
            if (!block)
                break;
            blob.Append(block);
        }

        LOG_INFO("Snapshot downloaded successfully");
    } catch (...) {
        Operation->Snapshot().Reset();
        throw;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
