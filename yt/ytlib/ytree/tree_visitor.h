#pragma once

#include "public.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

void VisitTree(INodePtr root, IYsonConsumer* consumer, bool visitAttributes = true);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
