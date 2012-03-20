#include "stdafx.h"
#include "job_manager.h"
//#include "unsafe_environment.h"
#include "private.h"

#include <ytlib/misc/fs.h>

//#include <yt/tallyman.h>
//#include <system_error>

namespace NYT {
namespace NExecAgent {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ExecAgentLogger;

////////////////////////////////////////////////////////////////////////////////
/*
TJobManager::TJobManager(
    TConfig* config,
    NChunkHolder::TChunkCache* chunkCache,
    NRpc::IChannel* masterChannel)
    : Config(config)
    , JobManagerThread(New<TActionQueue>("JobManager"))
    , EnvironmentManager(config)
    , ChunkCache(chunkCache)
    , MasterChannel(masterChannel)
    // make special "scheduler channel" that asks master for scheduler 
    , SchedulerProxy(~NRpc::CreateBusChannel(Config->SchedulerAddress))
{
    using namespace std;
    VERIFY_INVOKER_AFFINITY(JobManagerThread->GetInvoker(), JobManagerThread);

    try {
        tallyman_start( "/export/home/yeti/projects/wallet" );
    }
    catch ( const system_error& err ) {
        LOG_DEBUG("Failed to start tallyman: %s", err.what());
    }
    catch ( const runtime_error& err ) {
        LOG_DEBUG("Failed to start tallyman: %s", err.what());
    }

    // Init job slots.
    for (int slotIndex = 0; slotIndex < Config->SlotCount; ++ slotIndex) {
        auto slotName = Sprintf("slot.%d", slotIndex);
        auto location = NFS::CombinePaths(Config->SlotLocation, slotName);

        Slots.push_back(New<TSlot>(~Config->Slot, location, slotName));
    }

    // Register environment builders.
    EnvironmentManager.Register(
        "unsafe", 
        ~New<TUnsafeEnvironmentBuilder>());
}

TJobManager::~TJobManager()
{
    tallyman_stop();
}

IInvoker::TPtr TJobManager::GetInvoker()
{
    return JobManagerThread->GetInvoker();
}

void TJobManager::StartJob(
    const TJobId& jobId,
    const NScheduler::NProto::TJobSpec& jobSpec)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);

    TSlot::TPtr emptySlot;
    FOREACH(auto slot, Slots) {
        if (slot->IsEmpty()) {
            emptySlot = slot;
            break;
        }
    }

    if (!emptySlot) {
        LOG_WARNING("All slots are busy (JobId: %s)",
            ~jobId.ToString());

        NScheduler::NProto::TJobResult result;
        result.set_is_ok(false);
        result.set_error_message("All slots are busy.");

        OnJobFinished(result, jobId);
        return;
    }

    LOG_DEBUG("Found slot for new job (JobId: %s, working directory: %s)", 
        ~jobId.ToString(),
        ~emptySlot->GetWorkingDirectory());

    IProxyController* proxyController(NULL);
    try {
        proxyController = EnvironmentManager.CreateProxyController(
            //XXX: type of execution environment must not be directly
            // selectable by user -- it is more of the global cluster
            // setting
            //jobSpec.operation_spec().environment(),
            "default",
            jobId,
            emptySlot->GetWorkingDirectory());

    } catch (yexception& ex) {
        LOG_DEBUG("Failed to create proxy controller: (JobId: %s, Error: %s)", 
            ~jobId.ToString(),
            ex.what());

        NScheduler::NProto::TJobResult result;
        result.set_is_ok(false);
        result.set_error_message(Sprintf(
            "Failed to create proxy controller: %s",
            ex.what()));

        OnJobFinished(result, jobId);
        return;
    }

    // ToDo: handle errors.
    emptySlot->Acquire();

    auto job = New<TJob>(
        ~Config->Job,
        jobId,
        jobSpec,
        ~ChunkCache,
        ~MasterChannel,
        ~emptySlot,
        proxyController);

    Jobs[jobId] = job;

    job->SubscribeOnStarted(FromMethod(
        &TJobManager::OnJobStarted,
        TPtr(this),
        jobId)->Via(JobManagerThread->GetInvoker()));

    job->SubscribeOnFinished(FromMethod(
        &TJobManager::OnJobFinished,
        TPtr(this),
        jobId)->Via(JobManagerThread->GetInvoker()));

    LOG_DEBUG("Job created (JobId: %s)", ~jobId.ToString());
}

void TJobManager::OnJobStarted(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);

    auto req = SchedulerProxy.JobStarted();
    *(req->mutable_job_id()) = jobId.ToProto();

    // ToDo: check results, log errors, do retries.
    req->Invoke();
}

void TJobManager::CancelOperation(const TOperationId& operationId)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);
    FOREACH(auto& job, Jobs) {
        if (job.First().OperationId == operationId) {
            job.Second()->Cancel(TError("Job cancelled by sheduler."));
        }
    }

}

void TJobManager::OnJobFinished(
    NScheduler::NProto::TJobResult jobResult,
    const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);

    LOG_DEBUG("Job finished (JobId: %s)", ~jobId.ToString());

    Jobs.erase(jobId);

    auto req = SchedulerProxy.JobFinished();
    *(req->mutable_job_id()) = jobId.ToProto();
    *(req->mutable_job_result()) = jobResult;

    // ToDo: check results, log errors, do retries.
    req->Invoke();
}

TJob::TPtr TJobManager::GetJob(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);

    auto it = Jobs.find(jobId);
    if (it == Jobs.end()) {
        ythrow NRpc::TServiceException(EErrorCode::NoSuchJob) <<
            Sprintf("No job %s in active jobs.", ~jobId.ToString());
    }

    return it->second;
}

void TJobManager::SetJobResult(
    const TJobId& jobId, 
    const NScheduler::NProto::TJobResult& jobResult)
{
    auto job = GetJob(jobId);

    job->SetResult(jobResult);
}

const NScheduler::NProto::TJobSpec& 
TJobManager::GetJobSpec(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(JobManagerThread);

    auto job = GetJob(jobId);
    return job->GetSpec();
}
*/
////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NExecAgent
