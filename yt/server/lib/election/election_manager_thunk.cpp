#include "election_manager_thunk.h"

#include <yt/core/yson/producer.h>

namespace NYT::NElection {

////////////////////////////////////////////////////////////////////////////////

void TElectionManagerThunk::Initialize()
{
    Underlying_->Initialize();
}

void TElectionManagerThunk::Finalize()
{
    Underlying_->Finalize();
}

void TElectionManagerThunk::Participate()
{
    Underlying_->Participate();
}

void TElectionManagerThunk::Abandon()
{
    Underlying_->Abandon();
}

void TElectionManagerThunk::Restart()
{
    Underlying_->Restart();
}

NYson::TYsonProducer TElectionManagerThunk::GetMonitoringProducer()
{
    return Underlying_->GetMonitoringProducer();
}

void TElectionManagerThunk::SetUnderlying(IElectionManagerPtr underlying)
{
    Underlying_ = std::move(underlying);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NElection

