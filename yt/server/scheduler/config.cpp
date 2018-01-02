#include "config.h"

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

TFairShareStrategyOperationControllerConfig::TFairShareStrategyOperationControllerConfig()
{
    RegisterParameter("max_concurrent_controller_schedule_job_calls", MaxConcurrentControllerScheduleJobCalls)
        .Default(10)
        .GreaterThan(0);

    RegisterParameter("schedule_job_time_limit", ScheduleJobTimeLimit)
        .Default(TDuration::Seconds(60));

    RegisterParameter("schedule_job_fail_backoff_time", ScheduleJobFailBackoffTime)
        .Default(TDuration::MilliSeconds(100));
}

////////////////////////////////////////////////////////////////////////////////

TFairShareStrategyTreeConfig::TFairShareStrategyTreeConfig()
{
    RegisterParameter("nodes_filter", NodesFilter)
        .Default();

    RegisterParameter("min_share_preemption_timeout", MinSharePreemptionTimeout)
        .Default(TDuration::Seconds(15));
    RegisterParameter("fair_share_preemption_timeout", FairSharePreemptionTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("fair_share_starvation_tolerance", FairShareStarvationTolerance)
        .InRange(0.0, 1.0)
        .Default(0.8);

    RegisterParameter("min_share_preemption_timeout_limit", MinSharePreemptionTimeoutLimit)
        .Default(TDuration::Seconds(15));
    RegisterParameter("fair_share_preemption_timeout_limit", FairSharePreemptionTimeoutLimit)
        .Default(TDuration::Seconds(30));
    RegisterParameter("fair_share_starvation_tolerance_limit", FairShareStarvationToleranceLimit)
        .InRange(0.0, 1.0)
        .Default(0.8);

    RegisterParameter("max_unpreemptable_running_job_count", MaxUnpreemptableRunningJobCount)
        .Default(10);

    RegisterParameter("max_running_operation_count", MaxRunningOperationCount)
        .Default(200)
        .GreaterThan(0);

    RegisterParameter("max_running_operation_count_per_pool", MaxRunningOperationCountPerPool)
        .Alias("max_running_operations_per_pool")
        .Default(50)
        .GreaterThan(0);

    RegisterParameter("max_operation_count_per_pool", MaxOperationCountPerPool)
        .Alias("max_operations_per_pool")
        .Default(50)
        .GreaterThan(0);

    RegisterParameter("max_operation_count", MaxOperationCount)
        .Default(1000)
        .GreaterThan(0);

    RegisterParameter("enable_pool_starvation", EnablePoolStarvation)
        .Default(true);

    RegisterParameter("default_parent_pool", DefaultParentPool)
        .Default(RootPoolName);

    RegisterParameter("forbid_immediate_operations_in_root", ForbidImmediateOperationsInRoot)
        .Default(true);

    RegisterParameter("job_count_preemption_timeout_coefficient", JobCountPreemptionTimeoutCoefficient)
        .Default(1.0)
        .GreaterThanOrEqual(1.0);

    RegisterParameter("preemption_satisfaction_threshold", PreemptionSatisfactionThreshold)
        .Default(1.0)
        .GreaterThan(0);

    RegisterParameter("aggressive_preemption_satisfaction_threshold", AggressivePreemptionSatisfactionThreshold)
        .Default(0.5)
        .GreaterThan(0);

    RegisterParameter("enable_scheduling_tags", EnableSchedulingTags)
        .Default(true);

    RegisterParameter("heartbeat_tree_scheduling_info_log_period", HeartbeatTreeSchedulingInfoLogBackoff)
        .Default(TDuration::MilliSeconds(100));

    RegisterParameter("max_ephemeral_pools_per_user", MaxEphemeralPoolsPerUser)
        .GreaterThanOrEqual(1)
        .Default(5);

    RegisterParameter("update_preemptable_list_duration_logging_threshold", UpdatePreemptableListDurationLoggingThreshold)
        .Default(TDuration::MilliSeconds(100));

    RegisterParameter("enable_operations_profiling", EnableOperationsProfiling)
        .Default(true);

    RegisterParameter("threshold_to_enable_max_possible_usage_regularization", ThresholdToEnableMaxPossibleUsageRegularization)
        .InRange(0.0, 1.0)
        .Default(0.5);

    RegisterParameter("total_resource_limits_consider_delay", TotalResourceLimitsConsiderDelay)
        .Default(TDuration::Seconds(60));

    RegisterParameter("preemptive_scheduling_backoff", PreemptiveSchedulingBackoff)
        .Default(TDuration::Seconds(5));

    RegisterValidator([&] () {
        if (AggressivePreemptionSatisfactionThreshold > PreemptionSatisfactionThreshold) {
            THROW_ERROR_EXCEPTION("Aggressive preemption satisfaction threshold must be less than preemption satisfaction threshold")
                << TErrorAttribute("aggressive_threshold", AggressivePreemptionSatisfactionThreshold)
                << TErrorAttribute("threshold", PreemptionSatisfactionThreshold);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

TFairShareStrategyConfig::TFairShareStrategyConfig()
{
    RegisterParameter("fair_share_update_period", FairShareUpdatePeriod)
        .InRange(TDuration::MilliSeconds(10), TDuration::Seconds(60))
        .Default(TDuration::MilliSeconds(1000));

    RegisterParameter("fair_share_profiling_period", FairShareProfilingPeriod)
        .InRange(TDuration::MilliSeconds(10), TDuration::Seconds(60))
        .Default(TDuration::MilliSeconds(5000));

    RegisterParameter("fair_share_log_period", FairShareLogPeriod)
        .InRange(TDuration::MilliSeconds(10), TDuration::Seconds(60))
        .Default(TDuration::MilliSeconds(1000));

    RegisterParameter("min_needed_resources_update_period", MinNeededResourcesUpdatePeriod)
        .Default(TDuration::Seconds(3));

    RegisterParameter("max_operation_count", MaxOperationCount)
        .Default(5000)
        .GreaterThan(0);

    RegisterParameter("max_running_operation_count", MaxRunningOperationCount)
        .Alias("max_running_operations")
        .Default(1000)
        .GreaterThan(0);

    RegisterParameter("total_resource_limits_consider_delay", TotalResourceLimitsConsiderDelay)
        .Default();

    RegisterParameter("main_nodes_filter", MainNodesFilter)
        .Default();

    RegisterParameter("enable_operations_profiling", EnableOperationsProfiling)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

TSchedulerConfig::TSchedulerConfig()
{
    SetUnrecognizedStrategy(NYTree::EUnrecognizedStrategy::KeepRecursive);

    RegisterParameter("control_thread_priority", ControlThreadPriority)
        .Default();
    RegisterParameter("node_shard_count", NodeShardCount)
        .Default(4)
        .GreaterThan(0);

    RegisterParameter("connect_retry_backoff_time", ConnectRetryBackoffTime)
        .Default(TDuration::Seconds(15));
    RegisterParameter("node_heartbeat_timeout", NodeHeartbeatTimeout)
        .Default(TDuration::Seconds(60));
    RegisterParameter("watchers_update_period", WatchersUpdatePeriod)
        .Default(TDuration::Seconds(3));
    RegisterParameter("nodes_attributes_update_period", NodesAttributesUpdatePeriod)
        .Default(TDuration::Seconds(15));
    RegisterParameter("profiling_update_period", ProfilingUpdatePeriod)
        .Default(TDuration::Seconds(1));
    RegisterParameter("alerts_update_period", AlertsUpdatePeriod)
        .Default(TDuration::Seconds(1));
    RegisterParameter("node_shards_update_period", NodeShardsUpdatePeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("lock_transaction_timeout", LockTransactionTimeout)
        .Default(TDuration::Seconds(15));
    RegisterParameter("job_prober_rpc_timeout", JobProberRpcTimeout)
        .Default(TDuration::Seconds(300));

    RegisterParameter("cluster_info_logging_period", ClusterInfoLoggingPeriod)
        .Default(TDuration::Seconds(1));

    RegisterParameter("update_exec_node_descriptors_period", UpdateExecNodeDescriptorsPeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("jobs_logging_period", JobsLoggingPeriod)
        .Default(TDuration::Seconds(30));

    RegisterParameter("running_jobs_update_period", RunningJobsUpdatePeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("check_missing_jobs_period", CheckMissingJobsPeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("max_started_jobs_per_heartbeat", MaxStartedJobsPerHeartbeat)
        .Default()
        .GreaterThan(0);

    RegisterParameter("node_shard_exec_nodes_cache_update_period", NodeShardExecNodesCacheUpdatePeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("heartbeat_process_backoff", HeartbeatProcessBackoff)
        .Default(TDuration::MilliSeconds(5000));
    RegisterParameter("soft_concurrent_heartbeat_limit", SoftConcurrentHeartbeatLimit)
        .Default(50)
        .GreaterThanOrEqual(1);
    RegisterParameter("hard_concurrent_heartbeat_limit", HardConcurrentHeartbeatLimit)
        .Default(100)
        .GreaterThanOrEqual(1);

    // TODO(babenko): move to controller agent?
    RegisterParameter("job_spec_slice_throttler", JobSpecSliceThrottler)
        .Default(New<NConcurrency::TThroughputThrottlerConfig>(500000));

    RegisterParameter("static_orchid_cache_update_period", StaticOrchidCacheUpdatePeriod)
        .Default(TDuration::Seconds(1));

    RegisterParameter("orchid_keys_update_period", OrchidKeysUpdatePeriod)
        .Default(TDuration::Seconds(1));

    RegisterParameter("enable_job_reporter", EnableJobReporter)
        .Default(false);
    RegisterParameter("enable_job_spec_reporter", EnableJobSpecReporter)
        .Default(false);

    RegisterParameter("job_interrupt_timeout", JobInterruptTimeout)
        .Default(TDuration::Seconds(10));

    RegisterParameter("enable_unrecognized_alert", EnableUnrecognizedAlert)
        .Default(true);

    RegisterParameter("memory_distribution_different_node_types_threshold", MemoryDistributionDifferentNodeTypesThreshold)
        .Default(4);

    RegisterParameter("job_revival_abort_timeout", JobRevivalAbortTimeout)
        .Default(TDuration::Minutes(5));

    RegisterParameter("controller_agent_operation_rpc_timeout", ControllerAgentOperationRpcTimeout)
        .Default(TDuration::Seconds(1));

    RegisterValidator([&] () {
        if (SoftConcurrentHeartbeatLimit > HardConcurrentHeartbeatLimit) {
            THROW_ERROR_EXCEPTION("Soft limit on concurrent heartbeats must be less than or equal to hard limit on concurrent heartbeats")
                << TErrorAttribute("soft_limit", SoftConcurrentHeartbeatLimit)
                << TErrorAttribute("hard_limit", HardConcurrentHeartbeatLimit);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
