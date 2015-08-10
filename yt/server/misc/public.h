#pragma once

#include <core/misc/public.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class ECategory>
class TMemoryUsageTracker;

class TServerConfig;
typedef TIntrusivePtr<TServerConfig> TServerConfigPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
