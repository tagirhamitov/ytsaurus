#pragma once

#include "../misc/common.h"
#include "../misc/ptr.h"

#include "../actions/invoker.h"
#include "../actions/action.h"
#include "../actions/async_result.h"
#include "../actions/action_util.h"

#include "../logging/log.h"

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger RpcLogger;

////////////////////////////////////////////////////////////////////////////////

class TRpcManager
    : private TNonCopyable
{
public:
    TRpcManager();

    static TRpcManager* Get();
    NLog::TLogger& GetLogger();
    Stroka GetDebugInfo();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT

