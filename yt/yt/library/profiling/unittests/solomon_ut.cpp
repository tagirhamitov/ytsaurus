#include "yt/core/misc/ref_counted.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <yt/yt/library/profiling/sensor.h>
#include <yt/yt/library/profiling/impl.h>
#include <yt/yt/library/profiling/producer.h>

#include <yt/yt/library/profiling/solomon/registry.h>

#include <util/string/join.h>

namespace NYT::NProfiling {
namespace {

////////////////////////////////////////////////////////////////////////////////

struct TTestMetricConsumer
    : public NMonitoring::IMetricConsumer
{
    virtual void OnStreamBegin() override
    { }

    virtual void OnStreamEnd() override
    { }

    virtual void OnCommonTime(TInstant) override
    { }

    virtual void OnMetricBegin(NMonitoring::EMetricType) override
    { }

    virtual void OnMetricEnd() override
    { }

    virtual void OnLabelsBegin() override
    {
        Labels.clear();
    }

    virtual void OnLabelsEnd() override
    { }

    virtual void OnLabel(TStringBuf name, TStringBuf value)
    {
        if (name == "sensor") {
            Name = value;
        } else {
            Labels.emplace_back(TString(name) + "=" + value);
        }
    }

    virtual void OnDouble(TInstant, double value)
    {
        Cerr << FormatName() << " " << value << Endl;
        Gauges[FormatName()] = value;
    }

    virtual void OnUint64(TInstant, ui64)
    { }

    virtual void OnInt64(TInstant, i64 value)
    {
        Cerr << FormatName() << " " << value << Endl;
        Counters[FormatName()] = value;
    }

    virtual void OnHistogram(TInstant, NMonitoring::IHistogramSnapshotPtr) override
    { }

    virtual void OnLogHistogram(TInstant, NMonitoring::TLogHistogramSnapshotPtr) override
    { }

    virtual void OnSummaryDouble(TInstant, NMonitoring::ISummaryDoubleSnapshotPtr) override
    { }

    TString Name;
    std::vector<TString> Labels;

    THashMap<TString, i64> Counters;
    THashMap<TString, double> Gauges;

    TString FormatName() const
    {
        return Name + "{" + JoinSeq(";", Labels) + "}";
    }
};

////////////////////////////////////////////////////////////////////////////////

TEST(TSolomonRegistry, Registration)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/debug");

    auto counter = registry.Counter("/c0");
    auto gauge = registry.Gauge("/g0");

    impl->ProcessRegistrations();

    counter.Increment(1);
    gauge.Update(42);
}

TTestMetricConsumer Collect(TSolomonRegistryPtr impl, int subsample = 1, bool enableHack = false)
{
    impl->ProcessRegistrations();

    auto i = impl->GetNextIteration();
    impl->Collect();

    TTestMetricConsumer testConsumer;

    TReadOptions options;
    options.EnableSolomonAggregationWorkaround = enableHack;
    options.Times = {{{}, TInstant::Now()}};
    for (int j = subsample - 1; j >= 0; --j) {
        options.Times[0].first.push_back(impl->IndexOf(i - j));
    }

    impl->ReadSensors(options, &testConsumer);
    Cerr << "-------------------------------------" << Endl;

    return testConsumer;
}

TEST(TSolomonRegistry, CounterProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/d");

    auto c0 = registry.WithTag("user", "u0").Counter("/count");
    auto c1 = registry.WithTag("user", "u1").Counter("/count");

    auto result = Collect(impl).Counters;

    ASSERT_EQ(result["yt.d.count{}"], 0u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 0u);

    c0.Increment();
    c1.Increment();

    result = Collect(impl).Counters;

    ASSERT_EQ(result["yt.d.count{}"], 2u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 1u);

    c0.Increment();
    c1 = {};

    result = Collect(impl).Counters;
    ASSERT_EQ(result["yt.d.count{}"], 3u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 2u);
    ASSERT_EQ(result.find("yt.d.count{user=u1}"), result.end());

    Collect(impl, 2);
    Collect(impl, 3);
}

TEST(TSolomonRegistry, GaugeProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/d");

    auto g0 = registry.WithTag("user", "u0").Gauge("/memory");
    auto g1 = registry.WithTag("user", "u1").Gauge("/memory");

    auto result = Collect(impl).Gauges;

    ASSERT_EQ(result["yt.d.memory{}"], 0.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 0.0);

    g0.Update(1.0);
    g1.Update(2.0);

    result = Collect(impl).Gauges;
    ASSERT_EQ(result["yt.d.memory{}"], 3.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 1.0);

    g0.Update(10.0);
    g1 = {};

    result = Collect(impl).Gauges;
    ASSERT_EQ(result["yt.d.memory{}"], 10.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 10.0);
    ASSERT_EQ(result.find("yt.d.memory{user=u1}"), result.end());

    Collect(impl, 2);
    Collect(impl, 3);
}

TEST(TSolomonRegistry, SparseCounters)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/d");

    auto c = registry.WithSparse().Counter("/sparse_counter");

    auto result = Collect(impl).Counters;
    ASSERT_TRUE(result.empty());

    c.Increment();
    result = Collect(impl).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter{}"], 1u);

    result = Collect(impl).Counters;
    ASSERT_TRUE(result.empty());

    Collect(impl, 2);
    Collect(impl, 3);

    c.Increment();
    result = Collect(impl).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter{}"], 2u);
}

TEST(TSolomonRegistry, SparseCountersWithHack)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/d");

    auto c = registry.WithSparse().Counter("/sparse_counter_with_hack");

    auto result = Collect(impl, 1, true).Counters;
    ASSERT_TRUE(result.empty());

    c.Increment();
    result = Collect(impl, 1, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = Collect(impl, 2, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = Collect(impl, 3, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = Collect(impl, 3, true).Counters;
    ASSERT_TRUE(result.empty());
}

TEST(TSolomonRegistry, SparseGauge)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry registry(impl, "yt/d");

    auto c = registry.WithSparse().Gauge("/sparse_gauge");

    auto result = Collect(impl).Gauges;
    ASSERT_TRUE(result.empty());

    c.Update(1.0);
    result = Collect(impl).Gauges;
    ASSERT_EQ(result["yt.d.sparse_gauge{}"], 1.0);

    c.Update(0.0);
    result = Collect(impl).Gauges;
    ASSERT_TRUE(result.empty());

    Collect(impl, 2);
    Collect(impl, 3);
}

TEST(TSolomonRegistry, InvalidSensors)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry r(impl, "yt/d");

    auto invalidTypeCounter = r.Counter("/invalid_type");
    auto invalidTypeGauge = r.Gauge("/invalid_type");

    auto invalidSettingsCounter0 = r.Counter("/invalid_settings");
    auto invalidSettingsCounter1 = r.WithGlobal().Counter("/invalid_settings");

    auto result = Collect(impl);
    ASSERT_TRUE(result.Counters.empty());
    ASSERT_TRUE(result.Gauges.empty());

    Collect(impl, 2);
    Collect(impl, 3);
}

struct TDebugProducer
    : public ISensorProducer
{
    TSensorBuffer Buffer;

    virtual ~TDebugProducer()
    { }

    virtual void Collect(ISensorWriter* writer) override
    {
        Buffer.WriteTo(writer);
    }
};

TEST(TSolomonRegistry, GaugeProducer)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry r(impl, "yt/d");

    auto p0 = New<TDebugProducer>();
    r.AddProducer("/cpu", p0);

    auto p1 = New<TDebugProducer>();
    r.AddProducer("/cpu", p1);

    auto result = Collect(impl).Gauges;
    ASSERT_TRUE(result.empty());

    p0->Buffer.PushTag(std::pair<TString, TString>{"thread", "Control"});
    p0->Buffer.AddGauge("/user_time", 98);
    p0->Buffer.AddGauge("/system_time", 15);

    p1->Buffer.PushTag(std::pair<TString, TString>{"thread", "Profiler"});
    p1->Buffer.AddGauge("/user_time", 2);
    p1->Buffer.AddGauge("/system_time", 25);

    result = Collect(impl).Gauges;
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Control}"], 98.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Profiler}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{}"], 100.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Control}"], 15.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Profiler}"], 25.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{}"], 40.0);

    p0 = {};
    result = Collect(impl).Gauges;
    ASSERT_EQ(result.size(), static_cast<size_t>(4));
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Profiler}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Profiler}"], 25.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{}"], 25.0);

    Collect(impl, 2);
    Collect(impl, 3);
}

TEST(TSolomonRegistry, CustomProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry r(impl, "yt/d");

    auto c0 = r.Counter("/simple_sharded");
    c0.Increment();

    auto c1 = r.Counter("/simple_sharded");
    c1.Increment();

    auto g0 = r.WithExcludedTag("node_shard", "0").Gauge("/excluded_tag");
    g0.Update(10);

    auto g1 = r.WithExcludedTag("node_shard", "1").Gauge("/excluded_tag");
    g1.Update(20);

    auto c2 = r
        .WithRequiredTag("bundle", "sys")
        .WithTag("table_path", "//sys/operations")
        .Counter("/request_count");
    c2.Increment();

    auto c3 = r
        .WithTag("medium", "ssd")
        .WithTag("disk", "ssd0", -1)
        .Counter("/iops");
    c3.Increment();

    auto result = Collect(impl);
    ASSERT_EQ(result.Counters["yt.d.simple_sharded{}"], 2u);

    ASSERT_EQ(result.Gauges["yt.d.excluded_tag{}"], 30.0);
    ASSERT_EQ(result.Gauges.size(), static_cast<size_t>(1));

    ASSERT_EQ(result.Counters["yt.d.request_count{bundle=sys}"], 1u);
    ASSERT_EQ(result.Counters["yt.d.request_count{bundle=sys;table_path=//sys/operations}"], 1u);
    ASSERT_TRUE(result.Counters.find("yt.d.request_count{}") == result.Counters.end());
    ASSERT_TRUE(result.Counters.find("yt.d.request_count{table_path=//sys/operations}") == result.Counters.end());

    Collect(impl, 2);
    Collect(impl, 3);
}

DECLARE_REFCOUNTED_STRUCT(TBadProducer)

struct TBadProducer
    : public ISensorProducer
{
    virtual void Collect(ISensorWriter*)
    {
        THROW_ERROR_EXCEPTION("Unavailable");
    }
};

DEFINE_REFCOUNTED_TYPE(TBadProducer)

TEST(TSolomonRegistry, Exceptions)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TRegistry r(impl, "yt/d");

    auto producer = New<TBadProducer>();
    r.AddProducer("/p", producer);
    r.AddFuncCounter("/c", producer, [] () -> i64 {
        THROW_ERROR_EXCEPTION("Unavailable");
    });
    r.AddFuncGauge("/g", producer, [] () -> double {
        THROW_ERROR_EXCEPTION("Unavailable");
    });

    impl->ProcessRegistrations();
    impl->Collect();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NProfiling
