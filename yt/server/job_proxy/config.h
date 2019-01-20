#pragma once

#include "public.h"

#include <yt/server/exec_agent/config.h>

#include <yt/server/lib/misc/config.h>

#include <yt/ytlib/cgroup/config.h>

#include <yt/client/file_client/config.h>

#include <yt/ytlib/hydra/config.h>

#include <yt/ytlib/scheduler/config.h>

#include <yt/core/bus/tcp/config.h>

#include <yt/core/net/address.h>

#include <yt/core/ytree/node.h>
#include <yt/core/ytree/yson_serializable.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

class TJobThrottlerConfig
    : public NYTree::TYsonSerializable
{
public:
    TDuration MinBackoffTime;
    TDuration MaxBackoffTime;
    double BackoffMultiplier;

    TDuration RpcTimeout;

    TJobThrottlerConfig()
    {
        RegisterParameter("min_backoff_time", MinBackoffTime)
            .Default(TDuration::MilliSeconds(100));

        RegisterParameter("max_backoff_time", MaxBackoffTime)
            .Default(TDuration::Seconds(60));

        RegisterParameter("backoff_multiplier", BackoffMultiplier)
            .Default(1.5);

        RegisterParameter("rpc_timeout", RpcTimeout)
            .Default(TDuration::Seconds(5));
    }
};

DEFINE_REFCOUNTED_TYPE(TJobThrottlerConfig)

////////////////////////////////////////////////////////////////////////////////

class TJobProxyConfig
    : public TServerConfig
{
public:
    // Job-specific parameters.
    int SlotIndex = -1;

    std::optional<TString> TmpfsPath;
    std::vector<NExecAgent::TBindConfigPtr> Binds;

    std::vector<TString> GpuDevices;

    //! Path for container root.
    std::optional<TString> RootPath;

    // Job-independent parameters.
    NApi::NNative::TConnectionConfigPtr ClusterConnection;

    NBus::TTcpBusClientConfigPtr SupervisorConnection;
    TDuration SupervisorRpcTimeout;

    TDuration HeartbeatPeriod;
    TDuration InputPipeBlinkerPeriod;

    NYTree::INodePtr JobEnvironment;

    //! Addresses derived from node local descriptor to leverage locality.
    NNodeTrackerClient::TAddressMap Addresses;
    std::optional<TString> Rack;
    std::optional<TString> DataCenter;

    TDuration CoreForwarderTimeout;

    i64 AheadMemoryReserve;

    bool TestRootFS;

    TJobThrottlerConfigPtr JobThrottler;

    TJobProxyConfig()
    {
        RegisterParameter("slot_index", SlotIndex);

        RegisterParameter("tmpfs_path", TmpfsPath)
            .Default();

        RegisterParameter("root_path", RootPath)
            .Default();

        RegisterParameter("binds", Binds)
            .Default();

        RegisterParameter("gpu_devices", GpuDevices)
            .Default();

        RegisterParameter("cluster_connection", ClusterConnection);

        RegisterParameter("supervisor_connection", SupervisorConnection);

        RegisterParameter("supervisor_rpc_timeout", SupervisorRpcTimeout)
            .Default(TDuration::Seconds(30));

        RegisterParameter("heartbeat_period", HeartbeatPeriod)
            .Default(TDuration::Seconds(5));

        RegisterParameter("input_pipe_blinker_period", InputPipeBlinkerPeriod)
            .Default(TDuration::Seconds(1));

        RegisterParameter("job_environment", JobEnvironment);

        RegisterParameter("addresses", Addresses)
            .Default();

        RegisterParameter("rack", Rack)
            .Default();

        RegisterParameter("data_center", DataCenter)
            .Default();

        RegisterParameter("core_forwarder_timeout", CoreForwarderTimeout)
            .Default();

        RegisterParameter("ahead_memory_reserve", AheadMemoryReserve)
            .Default(100_MB);

        RegisterParameter("test_root_fs", TestRootFS)
            .Default(false);

        RegisterParameter("job_throttler", JobThrottler)
            .Default(nullptr);

    }
};

DEFINE_REFCOUNTED_TYPE(TJobProxyConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
