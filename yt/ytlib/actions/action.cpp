#include "stdafx.h"
#include "action.h"

#include "invoker.h"
#include "action_util.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

IAction::TPtr IAction::Via(IInvoker::TPtr invoker)
{
    YASSERT(invoker);

    return FromMethod(
        &IInvoker::Invoke,
        invoker,
        this);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
