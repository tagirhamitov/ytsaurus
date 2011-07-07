#pragma once

#include "../misc/common.h"
#include "../misc/ptr.h"

#include "../actions/invoker.h"
#include "../actions/action.h"
#include "../actions/async_result.h"
#include "../actions/action_util.h"

#include "../logging/log.h"

namespace NYT {
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

typedef TGUID TSessionId;
typedef i64 TSequenceId;

extern NLog::TLogger BusLogger;

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus
} // namespace NYT

