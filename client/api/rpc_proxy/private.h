#pragma once

#include <yt/core/logging/log.h>

#include <yt/core/profiling/profiler.h>

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TConnection)
DECLARE_REFCOUNTED_CLASS(TClientBase)
DECLARE_REFCOUNTED_CLASS(TClient)
DECLARE_REFCOUNTED_CLASS(TTransaction)
DECLARE_REFCOUNTED_CLASS(TDynamicChannelPool)

extern const NLogging::TLogger RpcProxyClientLogger;
extern const NProfiling::TProfiler RpcProxyClientProfiler;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
