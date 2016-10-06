#include "slot.h"
#include "private.h"
#include "config.h"
#include "job_environment.h"
#include "slot_location.h"

#include <yt/ytlib/job_prober_client/job_prober_service_proxy.h>
#include <yt/ytlib/job_prober_client/job_probe.h>

#include <yt/ytlib/cgroup/cgroup.h>

#include <yt/core/bus/tcp_client.h>

#include <yt/core/rpc/bus_channel.h>

#include <yt/core/concurrency/action_queue.h>

#include <yt/core/logging/log_manager.h>

#include <yt/core/misc/proc.h>

#include <yt/core/tools/tools.h>

#include <util/folder/dirut.h>

#include <util/system/fs.h>

namespace NYT {
namespace NExecAgent {

using namespace NJobProberClient;
using namespace NConcurrency;
using namespace NBus;
using namespace NRpc;
using namespace NTools;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TSlot
    : public ISlot
{
public:
    TSlot(int slotIndex, TSlotLocationPtr location, IJobEnvironmentPtr environment, const Stroka& nodeTag)
        : SlotIndex_(slotIndex)
        , JobEnvironment_(std::move(environment))
        , Location_(std::move(location))
        , NodeTag_(nodeTag)
    {
        Location_->IncreaseSessionCount();
    }

    virtual void Cleanup() override
    {
        // First kill all processes that may hold open handles to slot directories.
        JobEnvironment_->CleanProcesses(SlotIndex_);

        // After that clean the filesystem.
        WaitFor(Location_->CleanSandboxes(SlotIndex_))
            .ThrowOnError();
        Location_->DecreaseSessionCount();
    }

    virtual void CancelPreparation() override
    {
        PreparationCanceled_ = true;

        for (auto future : PreparationFutures_) {
            future.Cancel();
        }
    }

    virtual TFuture<void> RunJobProxy(
        NJobProxy::TJobProxyConfigPtr config,
        const TJobId& jobId,
        const TOperationId& operationId) override
    {
        JobProberClient_ = CreateJobProbe(GetRpcClientConfig(), jobId);
        return RunPrepareAction<void>([&] () {
            auto error = WaitFor(Location_->MakeConfig(SlotIndex_, ConvertToNode(config)));
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Failed to create job proxy config")

            return JobEnvironment_->RunJobProxy(
                SlotIndex_,
                Location_->GetSlotPath(SlotIndex_),
                jobId,
                operationId);
        });
    }

    virtual TFuture<void> MakeLink(
        ESandboxKind sandboxKind,
        const Stroka& targetPath,
        const Stroka& linkName,
        bool executable) override
    {
        return RunPrepareAction<void>([&] () {
            return Location_->MakeSandboxLink(
                SlotIndex_,
                sandboxKind,
                targetPath,
                linkName,
                executable);
        });
    }

    virtual TFuture<void> MakeCopy(
        ESandboxKind sandboxKind,
        const Stroka& sourcePath,
        const Stroka& destinationName,
        bool executable) override
    {
        return RunPrepareAction<void>([&] () {
            return Location_->MakeSandboxCopy(
                SlotIndex_,
                sandboxKind,
                sourcePath,
                destinationName,
                executable);
        });
    }

    virtual TFuture<Stroka> PrepareTmpfs(
        ESandboxKind sandboxKind,
        i64 size,
        Stroka path,
        bool enable) override
    {
        return RunPrepareAction<Stroka>([&] () {
            return Location_->MakeSandboxTmpfs(
                SlotIndex_,
                sandboxKind,
                size,
                JobEnvironment_->GetUserId(SlotIndex_),
                path,
                enable);
        },
        // Tmpfs mounting is uncancelable since it includes tool invokation in separate process.
        true);
    }

    virtual IJobProbePtr GetJobProberClient() override
    {
        YCHECK(JobProberClient_);
        return JobProberClient_;
    }

    virtual int GetSlotIndex() const override
    {
        return SlotIndex_;
    }

    virtual TTcpBusServerConfigPtr GetBusServerConfig() const override
    {
        auto unixDomainName = Format("%v-job-proxy-%v", NodeTag_, SlotIndex_);
        return TTcpBusServerConfig::CreateUnixDomain(unixDomainName);
    }

    virtual TFuture<void> CreateSandboxDirectories()
    {
        return RunPrepareAction<void>([&] () {
            return Location_->CreateSandboxDirectories(SlotIndex_);
        });
    }

private:
    const int SlotIndex_;
    IJobEnvironmentPtr JobEnvironment_;
    TSlotLocationPtr Location_;

    //! Uniquely identifies a node process on the current host.
    //! Used for unix socket name generation, to communicate between node and job proxies.
    const Stroka NodeTag_;

    IJobProbePtr JobProberClient_;

    std::vector<TFuture<void>> PreparationFutures_;
    bool PreparationCanceled_ = false;


    TTcpBusClientConfigPtr GetRpcClientConfig() const
    {
        auto unixDomainName = GetJobProxyUnixDomainName(NodeTag_, SlotIndex_);
        return TTcpBusClientConfig::CreateUnixDomain(unixDomainName);
    }

    template <class T>
    TFuture<T> RunPrepareAction(std::function<TFuture<T>()> action, bool uncancelable = false)
    {
        if (PreparationCanceled_) {
            return MakeFuture<T>(TError("Slot preparation canceled")
                << TErrorAttribute("slot_index", SlotIndex_));
        } else {
            auto future = action();
            auto preparationFuture = future.template As<void>();
            PreparationFutures_.push_back(uncancelable
                ? preparationFuture.ToUncancelable()
                : preparationFuture);
            return future;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

ISlotPtr CreateSlot(
    int slotIndex,
    TSlotLocationPtr location,
    IJobEnvironmentPtr environment,
    const Stroka& nodeTag)
{
    auto slot = New<TSlot>(
        slotIndex,
        std::move(location),
        std::move(environment),
        nodeTag);

    return slot;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent
} // namespace NYT
