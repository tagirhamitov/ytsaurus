#pragma once

#include "helpers.h"

#include <yt/yt/server/lib/controller_agent/serialize.h>

#include <yt/yt/server/lib/scheduler/scheduling_tag.h>

#include <yt/yt/core/misc/serialize.h>
#include <yt/yt/core/misc/statistics.h>

#include <yt/yt/core/logging/serializable_logger.h>

namespace NYT::NControllerAgent {

///////////////////////////////////////////////////////////////////////////////

struct TJobFinishedResult
{
    std::vector<TString> NewlyBannedTrees;
};

////////////////////////////////////////////////////////////////////////////////

//! This class encapsulates everything a task needs to know in order to decide
//! whether it's allowed to launch jobs in a tentative tree.
/*!
 *  There're several factors that may influence this decision:
 *    - pool tree configuration (viz. the "tentative" flag);
 *    - tentative job durations (in comparison to job durations in other pool trees).
  */
class TTentativeTreeEligibility
{
public:
    TTentativeTreeEligibility(const NScheduler::TTentativeTreeEligibilityConfigPtr& config, const NLogging::TLogger& logger);

    // For persistence only.
    TTentativeTreeEligibility();

    void Persist(const TPersistenceContext& context);

    bool CanScheduleJob(const TString& treeId, bool tentative);

    void OnJobStarted(const TString& treeId, bool tentative);

    //! No jobs in tentative trees can start after call to this method.
    void Disable();

    TJobFinishedResult OnJobFinished(
        const TJobSummary& jobSummary,
        const TString& treeId,
        bool tentative);

    std::vector<TString> FindAndBanSlowTentativeTrees();

    void LogTentativeTreeStatistics() const;

private:
    using TDurationSummary = TAvgSummary<TDuration>;

    TDurationSummary NonTentativeTreeDuration_;

    // Tentative job durations - by pool trees.
    THashMap<TString, TDurationSummary> Durations_;

    int SampleJobCount_ = -1;
    double MaxTentativeTreeJobDurationRatio_ = -1.0;
    TDuration MinJobDuration_;

    // Number of started/finished jobs per pool tree.
    THashMap<TString, int> StartedJobsPerPoolTree_;
    THashMap<TString, TInstant> LastStartJobTimePerPoolTree_;
    THashMap<TString, THashMap<EJobState, int>> FinishedJobsPerStatePerPoolTree_;

    THashSet<TString> BannedTrees_;

    bool Disabled_ = false;

    NLogging::TSerializableLogger Logger;

    // For documentation on the meaning of parameters, see
    // TTentativeTreeEligibilityConfig::{SampleJobCount,MaxTentativeJobDurationRatio,MinJobDuration} respectively.
    TTentativeTreeEligibility(int sampleJobCount, double maxTentativeJobDurationRatio, TDuration minJobDuration);

    TDuration GetTentativeTreeAverageJobDuration(const TString& treeId) const;

    void UpdateDurations(const TJobSummary& jobSummary, const TString& treeId, bool tentative);

    bool IsSlow(const TString& treeId) const;

    void BanTree(const TString& treeId);
    bool IsTreeBanned(const TString& treeId) const;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
