#include "stdafx.h"
#include "transient_state_manager.h"

#include "../actions/action_queue.h"
#include "../misc/property.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TTransientMetaStateManager
    : public IMetaStateManager
{
public:
    TTransientMetaStateManager(IMetaState* metaState)
        : MetaState(metaState)
    {
        Queue = New<TActionQueue>("StateManager");
    }

    void Start()
    {
        OnStartLeading_.Fire();
        OnLeaderRecoveryComplete_.Fire();
    }

    void Stop()
    {
        Queue->Shutdown();
    }

    EPeerStatus GetControlStatus() const
    {
        return EPeerStatus::Leading;
    }

    EPeerStatus GetStateStatus() const
    {
        return EPeerStatus::Leading;
    }

    IInvoker::TPtr GetStateInvoker()
    {
        return Queue->GetInvoker();
    }

    IInvoker::TPtr GetEpochStateInvoker()
    {
        return Queue->GetInvoker();
    }

    TAsyncCommitResult::TPtr CommitChange(
        const TSharedRef& changeData,
        IAction* changeAction = NULL)
    {
        if (changeAction == NULL) {
            MetaState->ApplyChange(changeData);
        } else {
            changeAction->Do();
        }
        return New<TAsyncCommitResult>(ECommitResult::Committed);
    }

    virtual void SetReadOnly(bool readOnly)
    {
        UNUSED(readOnly);
        YUNIMPLEMENTED();
    }

    void GetMonitoringInfo(NYTree::IYsonConsumer* consumer)
    {
        UNUSED(consumer);
        YUNIMPLEMENTED();
    }

    DEFINE_BYREF_RW_PROPERTY(TSignal, OnStartLeading);
    DEFINE_BYREF_RW_PROPERTY(TSignal, OnLeaderRecoveryComplete);
    DEFINE_BYREF_RW_PROPERTY(TSignal, OnStopLeading);
    DEFINE_BYREF_RW_PROPERTY(TSignal, OnStartFollowing);
    DEFINE_BYREF_RW_PROPERTY(TSignal, OnFollowerRecoveryComplete);
    DEFINE_BYREF_RW_PROPERTY(TSignal, OnStopFollowing);

private:
    TActionQueue::TPtr Queue;
    IMetaState::TPtr MetaState;
};

////////////////////////////////////////////////////////////////////////////////

IMetaStateManager::TPtr CreateTransientStateManager(
    IMetaState* metaState)
{
    return New<TTransientMetaStateManager>(metaState);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT


