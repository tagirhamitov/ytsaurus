#pragma once

#include "private.h"
#include "mutation_context.h"

#include <core/concurrency/thread_affinity.h>
#include <core/concurrency/periodic_executor.h>

#include <queue>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TResponseKeeper
    : public TRefCounted
{
public:
    TResponseKeeper(
        TResponseKeeperConfigPtr config,
        IInvokerPtr stateInvoker);

    bool FindResponse(const TMutationId& id, TSharedRef* data);
    void RegisterResponse(const TMutationId& id, const TSharedRef& data);
    void Clear();

private:
    TResponseKeeperConfigPtr Config;
    IInvokerPtr StateInvoker;
    NConcurrency::TPeriodicExecutorPtr SweepExecutor;

    typedef yhash_map<TMutationId, TSharedRef> TResponseMap;
    TResponseMap ResponseMap;

    struct TItem
    {
        TResponseMap::iterator Iterator;
        TInstant When;
    };

    typedef std::queue<TItem> TResponseQueue;
    TResponseQueue ResponseQueue;

    void TrySweep();
    void UpdateCounters(const TSharedRef& data, int delta);

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
