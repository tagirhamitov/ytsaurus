#pragma once

#include "public.h"

#include <yt/core/ytree/public.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/ytlib/chunk_client/public.h>

#include <yt/ytlib/api/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

NYPath::TYPath GetOperationsPath();
NYPath::TYPath GetOperationPath(const TOperationId& operationId);
NYPath::TYPath GetOperationProgressFromOrchid(const TOperationId& operationId);
NYPath::TYPath GetJobsPath(const TOperationId& operationId);
NYPath::TYPath GetJobPath(const TOperationId& operationId, const TJobId& jobId);
NYPath::TYPath GetStderrPath(const TOperationId& operationId, const TJobId& jobId);
NYPath::TYPath GetSnapshotPath(const TOperationId& operationId);
NYPath::TYPath GetSecureVaultPath(const TOperationId& operationId);
NYPath::TYPath GetFailContextPath(const TOperationId& operationId, const TJobId& jobId);
NYPath::TYPath GetNewFailContextPath(const TOperationId& operationId, const TJobId& jobId);
NYPath::TYPath GetLivePreviewOutputPath(const TOperationId& operationId, int tableIndex);
NYPath::TYPath GetLivePreviewStderrTablePath(const TOperationId& operationId);
NYPath::TYPath GetLivePreviewIntermediatePath(const TOperationId& operationId);

// TODO(babenko): remove "New" infix once we fully migrate to this scheme
NYPath::TYPath GetNewJobsPath(const TOperationId& operationId);
NYPath::TYPath GetNewJobPath(const TOperationId& operationId, const TJobId& jobId);
NYPath::TYPath GetNewOperationPath(const TOperationId& operationId);
NYPath::TYPath GetNewSecureVaultPath(const TOperationId& operationId);
NYPath::TYPath GetNewSnapshotPath(const TOperationId& operationId);
NYPath::TYPath GetNewStderrPath(const TOperationId& operationId, const TJobId& jobId);

std::vector<NYPath::TYPath> GetJobPaths(
    const TOperationId& operationId,
    const TJobId& jobId,
    bool enableCompatibleStorageMode,
    const TString& resourceName = {});

std::vector<NYPath::TYPath> GetOperationPaths(
    const TOperationId& operationId,
    bool enableCompatibleStorageMode,
    const TString& resourceName = {});

const NYPath::TYPath& GetPoolTreesPath();
const NYPath::TYPath& GetOperationsArchivePathOrderedById();
const NYPath::TYPath& GetOperationsArchivePathOrderedByStartTime();
const NYPath::TYPath& GetOperationsArchiveVersionPath();
const NYPath::TYPath& GetOperationsArchiveJobsPath();
const NYPath::TYPath& GetOperationsArchiveJobSpecsPath();
const NYPath::TYPath& GetOperationsArchiveJobStderrsPath();

bool IsOperationFinished(EOperationState state);
bool IsOperationFinishing(EOperationState state);
bool IsOperationInProgress(EOperationState state);

void ValidateEnvironmentVariableName(TStringBuf name);
bool IsOperationWithUserJobs(EOperationType operationType);

int GetJobSpecVersion();

bool IsSchedulingReason(EAbortReason reason);
bool IsNonSchedulingReason(EAbortReason reason);
bool IsSentinelReason(EAbortReason reason);

TError GetSchedulerTransactionsAbortedError(const std::vector<NObjectClient::TTransactionId>& transactionIds);
TError GetUserTransactionAbortedError(const NObjectClient::TTransactionId& transactionId);

////////////////////////////////////////////////////////////////////////////////

struct TJobFile
{
    TJobId JobId;
    NYPath::TYPath Path;
    NChunkClient::TChunkId ChunkId;
    TString DescriptionType;
};

void SaveJobFiles(NApi::INativeClientPtr client, const TOperationId& operationId, const std::vector<TJobFile>& files);

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
