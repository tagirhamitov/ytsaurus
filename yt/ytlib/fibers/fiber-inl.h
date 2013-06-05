#ifndef FIBER_INL_H_
#   error "Direct inclusion of this file is not allowed, include fiber.h"
#endif
#undef FIBER_INL_H_

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class T>
T WaitFor(TFuture<T> future, IInvokerPtr invoker)
{
    WaitFor(future.IgnoreResult(), std::move(invoker));
    return future.Get();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
