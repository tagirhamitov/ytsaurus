#pragma once

#include "private.h"
#include "serialize.h"

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

struct IJobSizeAdjuster
    : public virtual IPersistent
{
    virtual void UpdateStatistics(const TCompletedJobSummary& jobSummary) = 0;
    virtual void UpdateStatistics(i64 jobDataWeight, TDuration prepareDuration, TDuration execDuration) = 0;
    virtual i64 GetDataWeightPerJob() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IJobSizeAdjuster> CreateJobSizeAdjuster(
    i64 dataWeightPerJob,
    const TJobSizeAdjusterConfigPtr& config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
