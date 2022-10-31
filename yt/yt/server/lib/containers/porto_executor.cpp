#include "porto_executor.h"
#include "config.h"

#include "private.h"

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/misc/fs.h>

#include <yt/yt/core/profiling/profile_manager.h>
#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/ytree/convert.h>

#include <infra/porto/proto/rpc.pb.h>

#include <string>

namespace NYT::NContainers {

using namespace NConcurrency;
using Porto::EError;

////////////////////////////////////////////////////////////////////////////////

#ifdef _linux_

static const NLogging::TLogger& Logger = ContainersLogger;
static constexpr auto RetryInterval = TDuration::MilliSeconds(100);

////////////////////////////////////////////////////////////////////////////////

TString PortoErrorCodeFormatter(int code)
{
    return TEnumTraits<EPortoErrorCode>::ToString(static_cast<EPortoErrorCode>(code));
}

YT_DEFINE_ERROR_CODE_RANGE(12000, 13999, "NYT::NContainers::EPortoErrorCode", PortoErrorCodeFormatter);

////////////////////////////////////////////////////////////////////////////////

EPortoErrorCode ConvertPortoErrorCode(EError portoError)
{
    return static_cast<EPortoErrorCode>(PortoErrorCodeBase + portoError);
}

bool IsRetriableErrorCode(EPortoErrorCode error, bool idempotent)
{
    return
        error == EPortoErrorCode::Unknown ||
        // TODO(babenko): it's not obvious that we can always retry SocketError
        // but this is how it has used to work for a while.
        error == EPortoErrorCode::SocketError ||
        error == EPortoErrorCode::SocketTimeout && idempotent;
}

THashMap<TString, TErrorOr<TString>> ParsePortoGetResponse(
    const Porto::TGetResponse_TContainerGetListResponse& response)
{
    THashMap<TString, TErrorOr<TString>> result;
    for (const auto& property : response.keyval()) {
        if (property.error() == EError::Success) {
            result[property.variable()] = property.value();
        } else {
            result[property.variable()] = TError(ConvertPortoErrorCode(property.error()), property.errormsg())
                << TErrorAttribute("porto_error", ConvertPortoErrorCode(property.error()));
        }
    }
    return result;
}

THashMap<TString, TErrorOr<TString>> ParseSinglePortoGetResponse(
    const TString& name,
    const Porto::TGetResponse& getResponse)
{
    for (const auto& container : getResponse.list()) {
        if (container.name() == name) {
            return ParsePortoGetResponse(container);
        }
    }
    THROW_ERROR_EXCEPTION("Unable to get properties from Porto")
        << TErrorAttribute("container", name);
}

THashMap<TString, THashMap<TString, TErrorOr<TString>>> ParseMultiplePortoGetResponse(
    const Porto::TGetResponse& getResponse)
{
    THashMap<TString, THashMap<TString, TErrorOr<TString>>> result;
    for (const auto& container : getResponse.list()) {
        result[container.name()] = ParsePortoGetResponse(container);
    }
    return result;
}

TString FormatEnablePorto(EEnablePorto value)
{
    switch (value) {
        case EEnablePorto::None:    return "none";
        case EEnablePorto::Isolate: return "isolate";
        case EEnablePorto::Full:    return "full";
        default:                    YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

class TPortoExecutor
    : public IPortoExecutor
{
public:
    TPortoExecutor(
        TPortoExecutorConfigPtr config,
        const TString& threadNameSuffix,
        const NProfiling::TProfiler& profiler)
        : Config_(std::move(config))
        , Queue_(New<TActionQueue>(Format("Porto:%v", threadNameSuffix)))
        , Profiler_(profiler)
        , PollExecutor_(New<TPeriodicExecutor>(
            Queue_->GetInvoker(),
            BIND(&TPortoExecutor::DoPoll, MakeWeak(this)),
            Config_->PollPeriod))
    {
        Api_->SetTimeout(Config_->ApiTimeout.Seconds());
        Api_->SetDiskTimeout(Config_->ApiDiskTimeout.Seconds());

        PollExecutor_->Start();
    }

    TFuture<void> CreateContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoCreateContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> CreateContainer(const TRunnableContainerSpec& containerSpec, bool start) override
    {
        return BIND(&TPortoExecutor::DoCreateContainerFromSpec, MakeStrong(this), containerSpec, start)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<std::optional<TString>> GetContainerProperty(
        const TString& container,
        const TString& property) override
    {
        return BIND([=, this, this_ = MakeStrong(this)] () -> std::optional<TString> {
            auto response = DoGetContainerProperties({container}, {property});
            auto parsedResponse = ParseSinglePortoGetResponse(container, response);
            auto it = parsedResponse.find(property);
            if (it == parsedResponse.end()) {
                return std::nullopt;
            } else {
                return it->second.ValueOrThrow();
            }
        })
        .AsyncVia(Queue_->GetInvoker())
        .Run();
    }

    TFuture<THashMap<TString, TErrorOr<TString>>> GetContainerProperties(
        const TString& container,
        const std::vector<TString>& properties) override
    {
        return BIND([=, this, this_ = MakeStrong(this)] {
            auto response = DoGetContainerProperties({container}, properties);
            return ParseSinglePortoGetResponse(container, response);
        })
        .AsyncVia(Queue_->GetInvoker())
        .Run();
    }

    TFuture<THashMap<TString, THashMap<TString, TErrorOr<TString>>>> GetContainerProperties(
        const std::vector<TString>& containers,
        const std::vector<TString>& properties) override
    {
        return BIND([=, this, this_ = MakeStrong(this)] {
            auto response = DoGetContainerProperties(containers, properties);
            return ParseMultiplePortoGetResponse(response);
        })
        .AsyncVia(Queue_->GetInvoker())
        .Run();
    }

    TFuture<THashMap<TString, i64>> GetContainerMetrics(
        const std::vector<TString>& containers,
        const TString& metric) override
    {
        return BIND([=, this, this_ = MakeStrong(this)] {
            return DoGetContainerMetrics(containers, metric);
        })
        .AsyncVia(Queue_->GetInvoker())
        .Run();
    }

    TFuture<void> SetContainerProperty(
        const TString& container,
        const TString& property,
        const TString& value) override
    {
        return BIND(&TPortoExecutor::DoSetContainerProperty, MakeStrong(this), container, property, value)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> DestroyContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoDestroyContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> StopContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoStopContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> StartContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoStartContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<TString> ConvertPath(const TString& path, const TString& container) override
    {
        return BIND(&TPortoExecutor::DoConvertPath, MakeStrong(this), path, container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> KillContainer(const TString& container, int signal) override
    {
        return BIND(&TPortoExecutor::DoKillContainer, MakeStrong(this), container, signal)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<std::vector<TString>> ListSubcontainers(
        const TString& rootContainer,
        bool includeRoot) override
    {
        return BIND(&TPortoExecutor::DoListSubcontainers, MakeStrong(this), rootContainer, includeRoot)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<int> PollContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoPollContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<int> WaitContainer(const TString& container) override
    {
        return BIND(&TPortoExecutor::DoWaitContainer, MakeStrong(this), container)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    void SubscribeFailed(const TCallback<void (const TError&)>& callback) override
    {
        Failed_.Subscribe(callback);
    }

    void UnsubscribeFailed(const TCallback<void (const TError&)>& callback) override
    {
        Failed_.Unsubscribe(callback);
    }

    TFuture<TString> CreateVolume(
        const TString& path,
        const THashMap<TString, TString>& properties) override
    {
        return BIND(&TPortoExecutor::DoCreateVolume, MakeStrong(this), path, properties)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> LinkVolume(
        const TString& path,
        const TString& name) override
    {
        return BIND(&TPortoExecutor::DoLinkVolume, MakeStrong(this), path, name)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> UnlinkVolume(
        const TString& path,
        const TString& name) override
    {
        return BIND(&TPortoExecutor::DoUnlinkVolume, MakeStrong(this), path, name)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<std::vector<TString>> ListVolumePaths() override
    {
        return BIND(&TPortoExecutor::DoListVolumePaths, MakeStrong(this))
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> ImportLayer(const TString& archivePath, const TString& layerId, const TString& place) override
    {
        return BIND(&TPortoExecutor::DoImportLayer, MakeStrong(this), archivePath, layerId, place)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<void> RemoveLayer(const TString& layerId, const TString& place, bool async) override
    {
        return BIND(&TPortoExecutor::DoRemoveLayer, MakeStrong(this), layerId, place, async)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    TFuture<std::vector<TString>> ListLayers(const TString& place) override
    {
        return BIND(&TPortoExecutor::DoListLayers, MakeStrong(this), place)
            .AsyncVia(Queue_->GetInvoker())
            .Run();
    }

    IInvokerPtr GetInvoker() const override
    {
        return Queue_->GetInvoker();
    }

private:
    const TPortoExecutorConfigPtr Config_;
    const TActionQueuePtr Queue_;
    const NProfiling::TProfiler Profiler_;

    const std::unique_ptr<Porto::TPortoApi> Api_ = std::make_unique<Porto::TPortoApi>();
    const TPeriodicExecutorPtr PollExecutor_;

    std::vector<TString> Containers_;
    THashMap<TString, TPromise<int>> ContainerMap_;
    TSingleShotCallbackList<void(const TError&)> Failed_;

    struct TCommandEntry
    {
        explicit TCommandEntry(const NProfiling::TProfiler& registry)
            : TimeGauge(registry.Timer("/command_time"))
            , RetryCounter(registry.Counter("/command_retries"))
            , SuccessCounter(registry.Counter("/command_successes"))
            , FailureCounter(registry.Counter("/command_failures"))
        { }

        NProfiling::TEventTimer TimeGauge;
        NProfiling::TCounter RetryCounter;
        NProfiling::TCounter SuccessCounter;
        NProfiling::TCounter FailureCounter;
    };

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, CommandLock_);
    THashMap<TString, TCommandEntry> CommandToEntry_;

    static const std::vector<TString> ContainerRequestVars_;

    static TError CreatePortoError(EPortoErrorCode errorCode, const TString& message)
    {
        return TError(errorCode, "Porto API error")
            << TErrorAttribute("original_porto_error_code", static_cast<int>(errorCode) - PortoErrorCodeBase)
            << TErrorAttribute("porto_error_message", message);
    }

    void DoCreateContainer(const TString& container)
    {
        ExecuteApiCall(
            [&] { return Api_->Create(container); },
            "Create",
            /*idempotent*/ false);
    }

    void DoCreateContainerFromSpec(const TRunnableContainerSpec& spec, bool start)
    {
        Porto::TContainerSpec portoSpec;

        // Required properties.
        portoSpec.set_name(spec.Name);
        portoSpec.set_command(spec.Command);

        portoSpec.set_enable_porto(FormatEnablePorto(spec.EnablePorto));
        portoSpec.set_isolate(spec.Isolate);

        if (spec.StdinPath) {
            portoSpec.set_stdin_path(*spec.StdinPath);
        }
        if (spec.StdoutPath) {
            portoSpec.set_stdout_path(*spec.StdoutPath);
        }
        if (spec.StderrPath) {
            portoSpec.set_stderr_path(*spec.StderrPath);
        }

        if (spec.CurrentWorkingDirectory) {
            portoSpec.set_cwd(*spec.CurrentWorkingDirectory);
        }

        if (spec.CoreCommand) {
            portoSpec.set_core_command(*spec.CoreCommand);
        }
        if (spec.User) {
            portoSpec.set_user(*spec.User);
        }

        // Useful for jobs, where we operate with numeric group ids.
        if (spec.GroupId) {
            portoSpec.set_group(ToString(*spec.GroupId));
        }

        if (spec.ThreadLimit) {
            portoSpec.set_thread_limit(*spec.ThreadLimit);
        }

        if (spec.HostName) {
            // To get a reasonable and unique host name inside container.
            portoSpec.set_hostname(*spec.HostName);
            if (!spec.IPAddresses.empty()) {
                const auto& address = spec.IPAddresses[0];
                auto etcHosts = Format("%v %v\n", address, *spec.HostName);
                // To be able to resolve hostname into IP inside container.
                portoSpec.set_etc_hosts(etcHosts);
            }
        }

        if (!spec.IPAddresses.empty() && Config_->EnableNetworkIsolation) {
            // This label is intended for HBF-agent: YT-12512.
            auto* label = portoSpec.mutable_labels()->add_map();
            label->set_key("HBF.ignore_address");
            label->set_val("1");

            auto* netConfig = portoSpec.mutable_net()->add_cfg();
            netConfig->set_opt("L3");
            netConfig->add_arg("veth0");

            for (const auto& address : spec.IPAddresses) {
                auto* ipConfig = portoSpec.mutable_ip()->add_cfg();
                ipConfig->set_dev("veth0");
                ipConfig->set_ip(ToString(address));
            }

            if (spec.EnableNat64) {
                // Behave like nanny does.
                portoSpec.set_resolv_conf("nameserver fd64::1;nameserver 2a02:6b8:0:3400::5005;options attempts:1 timeout:1");
            }
        }

        for (const auto& [key, value] : spec.Labels) {
            auto* map = portoSpec.mutable_labels()->add_map();
            map->set_key(key);
            map->set_val(value);
        }

        for (const auto& [name, value] : spec.Env) {
            auto* var = portoSpec.mutable_env()->add_var();
            var->set_name(name);
            var->set_value(value);
        }

        for (const auto& controller : spec.CGroupControllers) {
            portoSpec.mutable_controllers()->add_controller(controller);
        }

        for (const auto& device : spec.Devices) {
            auto* portoDevice = portoSpec.mutable_devices()->add_device();
            portoDevice->set_device(device.DeviceName);
            portoDevice->set_access(device.Enabled ? "rw" : "-");
        }

        auto addBind = [&] (const TBind& bind) {
            auto* portoBind = portoSpec.mutable_bind()->add_bind();
            portoBind->set_target(bind.TargetPath);
            portoBind->set_source(bind.SourcePath);
            portoBind->add_flag(bind.IsReadOnly ? "ro" : "rw");
        };

        if (spec.RootFS) {
            portoSpec.set_root_readonly(spec.RootFS->IsRootReadOnly);
            portoSpec.set_root(spec.RootFS->RootPath);

            for (const auto& bind : spec.RootFS->Binds) {
                addBind(bind);
            }
        }

        {
            auto* ulimit = portoSpec.mutable_ulimit()->add_ulimit();
            ulimit->set_type("core");
            if (spec.EnableCoreDumps) {
                ulimit->set_unlimited(true);
            } else {
                ulimit->set_hard(0);
                ulimit->set_soft(0);
            }
        }

        // Set some universal defaults.
        portoSpec.set_oom_is_fatal(false);

        ExecuteApiCall(
            [&] { return Api_->CreateFromSpec(portoSpec, {}, start); },
            "CreateFromSpec",
            /*idempotent*/ false);
    }

    void DoSetContainerProperty(const TString& container, const TString& property, const TString& value)
    {
        ExecuteApiCall(
            [&] { return Api_->SetProperty(container, property, value); },
            "SetProperty",
            /*idempotent*/ true);
    }

    void DoDestroyContainer(const TString& container)
    {
        try {
            ExecuteApiCall(
                [&] { return Api_->Destroy(container); },
                "Destroy",
                /*idempotent*/ true);
        } catch (const TErrorException& ex) {
            if (!ex.Error().FindMatching(EPortoErrorCode::ContainerDoesNotExist)) {
                throw;
            }
        }
    }

    void DoStopContainer(const TString& container)
    {
        ExecuteApiCall(
            [&] { return Api_->Stop(container); },
            "Stop",
            /*idempotent*/ true);
    }

    void DoStartContainer(const TString& container)
    {
        ExecuteApiCall(
            [&] { return Api_->Start(container); },
            "Start",
            /*idempotent*/ false);
    }

    TString DoConvertPath(const TString& path, const TString& container)
    {
        TString result;
        ExecuteApiCall(
            [&] { return Api_->ConvertPath(path, container, "self", result); },
            "ConvertPath",
            /*idempotent*/ true);
        return result;
    }

    void DoKillContainer(const TString& container, int signal)
    {
        ExecuteApiCall(
            [&] { return Api_->Kill(container, signal); },
            "Kill",
            /*idempotent*/ false);
    }

    std::vector<TString> DoListSubcontainers(const TString& rootContainer, bool includeRoot)
    {
        Porto::TListContainersRequest req;
        auto filter = req.add_filters();
        filter->set_name(rootContainer + "/*");
        if (includeRoot) {
            auto rootFilter = req.add_filters();
            rootFilter->set_name(rootContainer);
        }
        auto fieldOptions = req.mutable_field_options();
        fieldOptions->add_properties("absolute_name");
        TVector<Porto::TContainer> containers;
        ExecuteApiCall(
            [&] { return Api_->ListContainersBy(req, containers); },
            "ListContainersBy",
            /*idempotent*/ true);

        std::vector<TString> containerNames;
        containerNames.reserve(containers.size());
        for (const auto& container : containers) {
            const auto& absoluteName = container.status().absolute_name();
            if (!absoluteName.empty()) {
                containerNames.push_back(absoluteName);
            }
        }
        return containerNames;
    }

    TFuture<int> DoWaitContainer(const TString& container)
    {
        auto result = NewPromise<int>();
        auto waitCallback = [=, this, this_ = MakeStrong(this)] (const Porto::TWaitResponse& rsp) {
            return OnContainerTerminated(rsp, result);
        };

        ExecuteApiCall(
            [&] { return Api_->AsyncWait({container}, {}, waitCallback); },
            "AsyncWait",
            /*idempotent*/ false);

        return result.ToFuture().ToImmediatelyCancelable();
    }

    void OnContainerTerminated(const Porto::TWaitResponse& portoWaitResponse, TPromise<int> result)
    {
        const auto& container = portoWaitResponse.name();
        const auto& state = portoWaitResponse.state();
        if (state != "dead" && state != "stopped") {
            result.TrySet(TError("Container finished with unexpected state")
                << TErrorAttribute("container_name", container)
                << TErrorAttribute("container_state", state));
            return;
        }

        GetContainerProperty(container, "exit_status").Apply(BIND(
            [=] (const TErrorOr<std::optional<TString>>& errorOrExitCode) {
                if (!errorOrExitCode.IsOK()) {
                    result.TrySet(TError("Container finished, but exit status is unknown")
                        << errorOrExitCode);
                    return;
                }

                const auto& optionalExitCode = errorOrExitCode.Value();
                if (!optionalExitCode) {
                    result.TrySet(TError("Container finished, but exit status is unknown")
                        << TErrorAttribute("container_name", container)
                        << TErrorAttribute("container_state", state));
                    return;
                }

                try {
                    int exitStatus = FromString<int>(*optionalExitCode);
                    result.TrySet(exitStatus);
                } catch (const std::exception& ex) {
                    auto error = TError("Failed to parse porto exit status")
                        << TErrorAttribute("container_name", container)
                        << TErrorAttribute("exit_status", optionalExitCode.value());
                    error.MutableInnerErrors()->push_back(TError(ex));
                    result.TrySet(error);
                }
            }));
    }

    TFuture<int> DoPollContainer(const TString& container)
    {
        auto [it, inserted] = ContainerMap_.insert({container, NewPromise<int>()});
        if (!inserted) {
            YT_LOG_WARNING("Container already added for polling (Container: %v)",
                container);
        } else {
            Containers_.push_back(container);
        }
        return it->second.ToFuture();
    }

    Porto::TGetResponse DoGetContainerProperties(
        const std::vector<TString>& containers,
        const std::vector<TString>& vars)
    {
        TVector<TString> containers_(containers.begin(), containers.end());
        TVector<TString> vars_(vars.begin(), vars.end());

        const Porto::TGetResponse* getResponse;

        ExecuteApiCall(
            [&] {
                getResponse = Api_->Get(containers_, vars_);
                return getResponse ? EError::Success : EError::Unknown;
            },
            "Get",
            /*idempotent*/ true);

        YT_VERIFY(getResponse);
        return *getResponse;
    }

    THashMap<TString, i64> DoGetContainerMetrics(
        const std::vector<TString>& containers,
        const TString& metric)
    {
        TVector<TString> containers_(containers.begin(), containers.end());

        TMap<TString, uint64_t> result;

        ExecuteApiCall(
            [&] { return Api_->GetProcMetric(containers_, metric, result); },
            "GetProcMetric",
            /*idempotent*/ true);

        return {result.begin(), result.end()};
    }

    void DoPoll()
    {
        try {
            if (Containers_.empty()) {
                return;
            }

            auto getResponse = DoGetContainerProperties(Containers_, ContainerRequestVars_);

            if (getResponse.list().empty()) {
                return;
            }

            auto getProperty = [] (
                const Porto::TGetResponse::TContainerGetListResponse& container,
                const TString& name) -> Porto::TGetResponse::TContainerGetValueResponse
            {
                for (const auto& property : container.keyval()) {
                    if (property.variable() == name) {
                        return property;
                    }
                }

                return {};
            };

            for (const auto& container : getResponse.list()) {
                auto state = getProperty(container, "state");
                if (state.error() == EError::ContainerDoesNotExist) {
                    HandleResult(container.name(), state);
                } else if (state.value() == "dead" || state.value() == "stopped") {
                    HandleResult(container.name(), getProperty(container, "exit_status"));
                }
                //TODO(dcherednik): other states
            }
        } catch (const std::exception& ex) {
            YT_LOG_ERROR(ex, "Fatal exception occurred while polling Porto");
            Failed_.Fire(TError(ex));
        }
    }

    TString DoCreateVolume(
        const TString& path,
        const THashMap<TString, TString>& properties)
    {
        auto volume = path;
        TMap<TString, TString> propertyMap(properties.begin(), properties.end());
        ExecuteApiCall(
            [&] { return Api_->CreateVolume(volume, propertyMap); },
            "CreateVolume",
            /*idempotent*/ false);
        return volume;
    }

    void DoLinkVolume(const TString& path, const TString& container)
    {
        ExecuteApiCall(
            [&] { return Api_->LinkVolume(path, container); },
            "LinkVolume",
            /*idempotent*/ false);
    }

    void DoUnlinkVolume(const TString& path, const TString& container)
    {
        ExecuteApiCall(
            [&] { return Api_->UnlinkVolume(path, container); },
            "UnlinkVolume",
            /*idempotent*/ false);
    }

    std::vector<TString> DoListVolumePaths()
    {
        TVector<TString> volumes;
        ExecuteApiCall(
            [&] { return Api_->ListVolumes(volumes); },
            "ListVolume",
            /*idempotent*/ true);
        return {volumes.begin(), volumes.end()};
    }

    void DoImportLayer(const TString& archivePath, const TString& layerId, const TString& place)
    {
        ExecuteApiCall(
            [&] { return Api_->ImportLayer(layerId, archivePath, false, place); },
            "ImportLayer",
            /*idempotent*/ false);
    }

    void DoRemoveLayer(const TString& layerId, const TString& place, bool async)
    {
        ExecuteApiCall(
            [&] { return Api_->RemoveLayer(layerId, place, async); },
            "RemoveLayer",
            /*idempotent*/ false);
    }

    std::vector<TString> DoListLayers(const TString& place)
    {
        TVector<TString> layers;
        ExecuteApiCall(
            [&] { return Api_->ListLayers(layers, place); },
            "ListLayers",
            /*idempotent*/ true);
        return {layers.begin(), layers.end()};
    }

    TCommandEntry* GetCommandEntry(const TString& command)
    {
        auto guard = Guard(CommandLock_);
        if (auto it = CommandToEntry_.find(command)) {
            return &it->second;
        }
        return &CommandToEntry_.emplace(command, TCommandEntry(Profiler_.WithTag("command", command))).first->second;
    }

    void ExecuteApiCall(
        std::function<EError()> callback,
        const TString& command,
        bool idempotent)
    {
        YT_LOG_DEBUG("Porto API call started (Command: %v)", command);

        auto* entry = GetCommandEntry(command);
        auto startTime = NProfiling::GetInstant();
        while (true) {
            EError error;
            {
                NProfiling::TWallTimer timer;
                error = callback();
                entry->TimeGauge.Record(timer.GetElapsedTime());
            }

            if (error == EError::Success) {
                entry->SuccessCounter.Increment();
                break;
            }

            entry->FailureCounter.Increment();
            HandleApiError(command, startTime, idempotent);

            YT_LOG_DEBUG("Sleeping and retrying Porto API call (Command: %v)", command);
            entry->RetryCounter.Increment();

            TDelayedExecutor::WaitForDuration(RetryInterval);
        }

        YT_LOG_DEBUG("Porto API call completed (Command: %v)", command);
    }

    void HandleApiError(
        const TString& command,
        TInstant startTime,
        bool idempotent)
    {
        TString errorMessage;
        auto error = ConvertPortoErrorCode(Api_->GetLastError(errorMessage));

        // These errors are typical during job cleanup: we might try to kill a container that is already stopped.
        bool debug = (error == EPortoErrorCode::ContainerDoesNotExist || error == EPortoErrorCode::InvalidState);
        YT_LOG_EVENT(
            Logger,
            debug ? NLogging::ELogLevel::Debug : NLogging::ELogLevel::Error,
            "Porto API call error (Error: %v, Command: %v, Message: %v)",
            error,
            command,
            errorMessage);

        if (!IsRetriableErrorCode(error, idempotent) || NProfiling::GetInstant() - startTime > Config_->RetriesTimeout) {
            THROW_ERROR CreatePortoError(error, errorMessage);
        }
    }

    void HandleResult(const TString& container, const Porto::TGetResponse::TContainerGetValueResponse& rsp)
    {
        auto portoErrorCode = ConvertPortoErrorCode(rsp.error());
        auto it = ContainerMap_.find(container);
        if (it == ContainerMap_.end()) {
            YT_LOG_ERROR("Got an unexpected container "
                "(Container: %v, ResponseError: %v, ErrorMessage: %v, Value: %v)",
                container,
                portoErrorCode,
                rsp.errormsg(),
                rsp.value());
            return;
        } else {
            if (portoErrorCode != EPortoErrorCode::Success) {
                YT_LOG_ERROR("Container finished with Porto API error "
                    "(Container: %v, ResponseError: %v, ErrorMessage: %v, Value: %v)",
                    container,
                    portoErrorCode,
                    rsp.errormsg(),
                    rsp.value());
                it->second.Set(CreatePortoError(portoErrorCode, rsp.errormsg()));
            } else {
                try {
                    int exitStatus = std::stoi(rsp.value());
                    YT_LOG_DEBUG("Container finished with exit code (Container: %v, ExitCode: %v)",
                        container,
                        exitStatus);

                    it->second.Set(exitStatus);
                } catch (const std::exception& ex) {
                    it->second.Set(TError("Failed to parse Porto exit status") << ex);
                }
            }
        }
        RemoveFromPoller(container);
    }

    void RemoveFromPoller(const TString& container)
    {
        ContainerMap_.erase(container);

        Containers_.clear();
        for (const auto& [name, pid] : ContainerMap_) {
            Containers_.push_back(name);
        }
    }
};

const std::vector<TString> TPortoExecutor::ContainerRequestVars_ = {
    "state",
    "exit_status"
};

////////////////////////////////////////////////////////////////////////////////

IPortoExecutorPtr CreatePortoExecutor(
    TPortoExecutorConfigPtr config,
    const TString& threadNameSuffix,
    const NProfiling::TProfiler& profiler)
{
    return New<TPortoExecutor>(
        std::move(config),
        threadNameSuffix,
        profiler);
}

////////////////////////////////////////////////////////////////////////////////

#else

IPortoExecutorPtr CreatePortoExecutor(
    TPortoExecutorConfigPtr /* config */,
    const TString& /* threadNameSuffix */,
    const NProfiling::TProfiler& /* profiler */)
{
    THROW_ERROR_EXCEPTION("Porto executor is not available on this platform");
}

#endif

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NContainers
