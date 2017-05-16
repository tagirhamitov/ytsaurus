#include "scheduler.h"
#include "fiber.h"
#include "fls.h"

namespace NYT {
namespace NConcurrency {

////////////////////////////////////////////////////////////////////////////////

PER_THREAD IScheduler* CurrentScheduler = nullptr;

IScheduler* GetCurrentScheduler()
{
    YCHECK(CurrentScheduler);
    return CurrentScheduler;
}

IScheduler* TryGetCurrentScheduler()
{
    return CurrentScheduler;
}

void SetCurrentScheduler(IScheduler* scheduler)
{
    YCHECK(!CurrentScheduler);
    CurrentScheduler = scheduler;
}

////////////////////////////////////////////////////////////////////////////////

PER_THREAD TFiberId CurrentFiberId = InvalidFiberId;

TFiberId GetCurrentFiberId()
{
    return CurrentFiberId;
}

void SetCurrentFiberId(TFiberId id)
{
    CurrentFiberId = id;
}

////////////////////////////////////////////////////////////////////////////////

void Yield()
{
    WaitFor(VoidFuture);
}

void SwitchTo(IInvokerPtr invoker)
{
    Y_ASSERT(invoker);
    GetCurrentScheduler()->SwitchTo(std::move(invoker));
}

////////////////////////////////////////////////////////////////////////////////

TContextSwitchGuard::TContextSwitchGuard(std::function<void()> handler)
    : Active_(true)
{
    GetCurrentScheduler()->PushContextSwitchHandler([this, handler = std::move(handler)] () noexcept {
        Y_ASSERT(Active_);
        handler();
        Active_ = false;
    });
}

TContextSwitchGuard::~TContextSwitchGuard()
{
    if (Active_) {
        GetCurrentScheduler()->PopContextSwitchHandler();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT

