#include "job_prober_service.h"
#include "private.h"
#include "scheduler.h"
#include "bootstrap.h"

#include <yt/ytlib/scheduler/job_prober_service_proxy.h>

#include <yt/core/rpc/service_detail.h>

#include <yt/core/ytree/permission.h>

#include <yt/core/misc/proc.h>

namespace NYT::NScheduler {

using namespace NRpc;
using namespace NConcurrency;
using namespace NYson;
using namespace NYTree;
using namespace NSecurityClient;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

class TJobProberService
    : public TServiceBase
{
public:
    explicit TJobProberService(TBootstrap* bootstrap)
        : TServiceBase(
            bootstrap->GetControlInvoker(EControlQueue::UserRequest),
            TJobProberServiceProxy::GetDescriptor(),
            SchedulerLogger)
        , Bootstrap_(bootstrap)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(DumpInputContext));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetJobNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbandonJob));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortJob));
    }

private:
    TBootstrap* const Bootstrap_;

    DECLARE_RPC_SERVICE_METHOD(NProto, DumpInputContext)
    {
        auto jobId = FromProto<TJobId>(request->job_id());
        const auto& path = request->path();
        context->SetRequestInfo("JobId: %v, Path: %v",
            jobId,
            path);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        WaitFor(scheduler->DumpInputContext(jobId, path, context->GetUser()))
            .ThrowOnError();

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, GetJobNode)
    {
        auto jobId = FromProto<TJobId>(request->job_id());
        auto requiredPermissions = EPermissionSet(request->required_permissions());
        context->SetRequestInfo("JobId: %v, RequiredPermissions: %v",
            jobId,
            requiredPermissions);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        auto jobNodeDescriptor = WaitFor(scheduler->GetJobNode(jobId, context->GetUser(), requiredPermissions))
            .ValueOrThrow();

        context->SetResponseInfo("NodeDescriptor: %v", jobNodeDescriptor);

        ToProto(response->mutable_node_descriptor(), jobNodeDescriptor);

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, AbandonJob)
    {
        auto jobId = FromProto<TJobId>(request->job_id());
        context->SetRequestInfo("JobId: %v", jobId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        WaitFor(scheduler->AbandonJob(jobId, context->GetUser()))
            .ThrowOnError();

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, AbortJob)
    {
        auto jobId = FromProto<TJobId>(request->job_id());
        auto interruptTimeout = request->has_interrupt_timeout()
            ? std::make_optional(FromProto<TDuration>(request->interrupt_timeout()))
            : std::nullopt;
        context->SetRequestInfo("JobId: %v, InterruptTimeout: %v",
            jobId,
            interruptTimeout);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        WaitFor(scheduler->AbortJob(jobId, interruptTimeout, context->GetUser()))
            .ThrowOnError();

        context->Reply();
    }
};

IServicePtr CreateJobProberService(TBootstrap* bootstrap)
{
    return New<TJobProberService>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
