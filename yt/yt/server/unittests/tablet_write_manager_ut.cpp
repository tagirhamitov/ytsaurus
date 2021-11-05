#include "tablet_write_manager_ut_helpers.h"

#include <yt/yt/client/table_client/row_buffer.h>

#include <util/string/split.h>

namespace NYT::NTabletNode {
namespace {

using namespace testing;

using namespace NTableClient;

////////////////////////////////////////////////////////////////////////////////

// Tests below check interaction between:
// - Tablet write manager
// - Transaction manager
// NB: original transaction supervisor is not used, simple transaction supervisor is used instead.

class TTestTabletWriteManager
    : public TTabletWriteManagerTestBase
{
protected:
    TTabletOptions GetOptions() const override
    {
        return TTabletOptions{};
    }

    TUnversionedOwningRow BuildRow(i64 key, std::optional<i64> value = std::nullopt)
    {
        TUnversionedOwningRowBuilder builder;
        builder.AddValue(MakeUnversionedInt64Value(key, /*id*/ 0));
        if (value) {
            builder.AddValue(MakeUnversionedInt64Value(*value, /*id*/ 1));
        }
        return builder.FinishRow();
    }

    TVersionedOwningRow BuildVersionedRow(i64 key, std::vector<std::pair<ui64, i64>> values)
    {
        TVersionedRowBuilder builder(RowBuffer_);
        builder.AddKey(MakeUnversionedInt64Value(key, /*id*/ 0));
        for (const auto& [timestamp, value] : values) {
            builder.AddValue(MakeVersionedInt64Value(value, timestamp, /*id*/ 1));
        }
        return TVersionedOwningRow(builder.FinishRow());
    }

    void RunRecoverRun(const std::function<void()>& callable)
    {
        callable();
        HydraManager()->SaveLoad();
        callable();
    }

private:
    TRowBufferPtr RowBuffer_ = New<TRowBuffer>();
};

////////////////////////////////////////////////////////////////////////////////

using TTestTabletWriteBasic = TTestTabletWriteManager;

TEST_F(TTestTabletWriteBasic, TestSimple)
{
    auto versionedTxId = MakeTabletTransactionId(TTimestamp(0x40));

    WaitFor(WriteVersionedRows(versionedTxId, {BuildVersionedRow(1, {{0x25, 1}})}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    PrepareTransactionCommit(versionedTxId, true, 0x50);
    CommitTransaction(versionedTxId, 0x60);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto result = VersionedLookupRow(BuildRow(1));
    EXPECT_EQ(
        ToString(BuildVersionedRow(1, {{0x25, 1}})),
        ToString(result));

    auto unversionedTxId = MakeTabletTransactionId(TTimestamp(0x70));

    WaitFor(WriteUnversionedRows(unversionedTxId, {BuildRow(1, 2)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    PrepareTransactionCommit(unversionedTxId, true, 0x80);
    CommitTransaction(unversionedTxId, 0x90);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    result = VersionedLookupRow(BuildRow(1));
    EXPECT_EQ(
        ToString(BuildVersionedRow(1, {{0x25, 1}, {0x90, 2}})),
        ToString(result));

    ExpectFullyUnlocked();
}

TEST_F(TTestTabletWriteBasic, TestConflictWithPrelockedRow)
{
    auto txId1 = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(txId1, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());

    auto txId2 = MakeTabletTransactionId(TTimestamp(0x11));

    EXPECT_THAT(
        [&] {
            WaitFor(WriteUnversionedRows(txId2, {BuildRow(1, 2)}))
                .ThrowOnError();
        },
        ThrowsMessage<std::exception>(HasSubstr("lock conflict due to concurrent write")));
}

TEST_F(TTestTabletWriteBasic, TestConflictWithLockedRowByLeader)
{
    auto txId1 = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(txId1, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto txId2 = MakeTabletTransactionId(TTimestamp(0x11));

    EXPECT_THAT(
        [&] {
            WaitFor(WriteUnversionedRows(txId2, {BuildRow(1, 2)}))
                .ThrowOnError();
        },
        ThrowsMessage<std::exception>(HasSubstr("lock conflict due to concurrent write")));
}

TEST_F(TTestTabletWriteBasic, TestConflictWithLockedRowByFollower)
{
    auto txId1 = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(txId1, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll(/*recovery*/ true);

    auto txId2 = MakeTabletTransactionId(TTimestamp(0x11));

    EXPECT_THAT(
        [&] {
            WaitFor(WriteUnversionedRows(txId2, {BuildRow(1, 2)}))
                .ThrowOnError();
        },
        ThrowsMessage<std::exception>(HasSubstr("lock conflict due to concurrent write")));
}

////////////////////////////////////////////////////////////////////////////////

using TTestTabletWriteBarrier = TTestTabletWriteManager;

TEST_F(TTestTabletWriteBarrier, TestWriteBarrierUnversionedPrepared)
{
    auto unversionedTxId = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(unversionedTxId, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    PrepareTransactionCommit(unversionedTxId, true, 0x20);

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto versionedTxId = MakeTabletTransactionId(TTimestamp(0x40));

    RunRecoverRun([&] {
        EXPECT_THAT(
            [&] {
                WaitFor(WriteVersionedRows(versionedTxId, {BuildVersionedRow(1, {{0x25, 2}})}))
                    .ThrowOnError();
            },
            ThrowsMessage<std::exception>(HasSubstr("user writes are still pending")));
    });
}

TEST_F(TTestTabletWriteBarrier, TestWriteBarrierUnversionedActive)
{
    auto unversionedTxId = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(unversionedTxId, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto versionedTxId = MakeTabletTransactionId(TTimestamp(0x40));

    RunRecoverRun([&] {
        EXPECT_THAT(
            [&] {
                WaitFor(WriteVersionedRows(versionedTxId, {BuildVersionedRow(1, {{0x25, 2}})}))
                    .ThrowOnError();
            },
            ThrowsMessage<std::exception>(HasSubstr("user writes are still pending")));
    });
}

TEST_F(TTestTabletWriteBarrier, TestWriteBarrierUnversionedInFlight)
{
    auto unversionedTxId = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(unversionedTxId, {BuildRow(1, 1)}))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());

    auto versionedTxId = MakeTabletTransactionId(TTimestamp(0x40));

    EXPECT_THAT(
        [&] {
            WaitFor(WriteVersionedRows(versionedTxId, {BuildVersionedRow(1, {{0x25, 2}})}))
                .ThrowOnError();
        },
        ThrowsMessage<std::exception>(HasSubstr("user mutations are still in flight")));

    // NB: in contrast to previous two tests, we cannot expect the same error after recovering from snapshot.
    // Note that WriteRows is not accepted during recovery.
}

////////////////////////////////////////////////////////////////////////////////

using TTestTabletWriteSignature = TTestTabletWriteManager;

TEST_F(TTestTabletWriteSignature, TestSignaturesSuccess)
{
    auto txId = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ 1))
        .ThrowOnError();

    WaitFor(WriteUnversionedRows(txId, {BuildRow(1, 42)}, /*signatures*/ FinalTransactionSignature - 1))
        .ThrowOnError();

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto asyncCommit = PrepareAndCommitTransaction(txId, true, 0x20);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    asyncCommit
        .Get()
        .ThrowOnError();

    EXPECT_EQ(
        ToString(BuildVersionedRow(0, {{0x20, 42}})),
        ToString(VersionedLookupRow(BuildRow(0))));
    EXPECT_EQ(
        ToString(BuildVersionedRow(1, {{0x20, 42}})),
        ToString(VersionedLookupRow(BuildRow(1))));

    ExpectFullyUnlocked();
}

TEST_F(TTestTabletWriteSignature, TestSignaturesFailure)
{
    auto txId = MakeTabletTransactionId(TTimestamp(0x10));

    WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ 1))
        .ThrowOnError();

    EXPECT_EQ(1, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    auto asyncCommit = PrepareAndCommitTransaction(txId, true, 0x20);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    EXPECT_THAT(
        [&] {
            asyncCommit
                .Get()
                .ThrowOnError();
        },
        ThrowsMessage<std::exception>(HasSubstr("expected signature")));
}

////////////////////////////////////////////////////////////////////////////////

class TTestTabletWriteGenerationSimple
    : public TTestTabletWriteManager
    , public testing::WithParamInterface<TStringBuf>
{ };

//! Used for fancy test name generation.
std::string FormatParameter(const TestParamInfo<TStringBuf>& info)
{
    return Format("%02u__%v", info.index, info.param);
}

////////////////////////////////////////////////////////////////////////////////

// Consider a scenario with two generations of the one-batch transaction. Assume that
// Wn stands for write of generation n, Ak stands for mutation application up to (k+1)-st mutation and
// R stands for recovery.
constexpr TStringBuf OneBatchExecutionPlans[] = {
    "W0_W1_A0_A1",
    "W0_W1_A0_A1_R",
    "W0_W1_A0_R_A1",
    "W0_W1_R_A0_A1",
    "W0_R_A0_W1_A1",
    "W0_A0_W1_A1",
    "W0_A0_W1_R_A1",
    "W0_R_A0_W1_A1",
};

using TTestTabletWriteGenerationOneBatch = TTestTabletWriteGenerationSimple;

TEST_P(TTestTabletWriteGenerationOneBatch, OneBatchRetry)
{
    const auto& executionPlan = GetParam();
    auto txId = MakeTabletTransactionId(TTimestamp(0x10));

    THashMap<TStringBuf, std::function<void()>> actions;

    actions["W0"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ FinalTransactionSignature))
            .ThrowOnError();
    };
    actions["W1"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ FinalTransactionSignature, /*generation*/ 1))
            .ThrowOnError();
    };
    actions["A0"] = [&] {
        HydraManager()->ApplyUpTo(1);
    };
    actions["A1"] = [&] {
        HydraManager()->ApplyUpTo(2);
    };
    actions["R"] = [&] {
        HydraManager()->SaveLoad();
    };

    // The row should be locked until the end of our transaction. We check that
    // by trying to perform a concurrent write. Note that writing during recovery is
    // invalid.
    auto validateRowLock = [&] {
        auto txId2 = MakeTabletTransactionId(TTimestamp(0x11));
        EXPECT_THAT(
            [&] {
                WaitFor(WriteUnversionedRows(txId2, {BuildRow(0, 43)}))
                    .ThrowOnError();
            },
            ThrowsMessage<std::exception>(HasSubstr("lock conflict due to concurrent write")));
    };

    for (const auto& token : StringSplitter(executionPlan).Split('_')) {
        const auto& action = GetOrCrash(actions, token);
        action();
        if (HydraManager()->IsActiveLeader()) {
            validateRowLock();
        }
    }

    auto asyncCommit = PrepareAndCommitTransaction(txId, true, 0x20);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    asyncCommit
        .Get()
        .ThrowOnError();

    EXPECT_EQ(
        ToString(BuildVersionedRow(0, {{0x20, 42}})),
        ToString(VersionedLookupRow(BuildRow(0))));

    ExpectFullyUnlocked();
}

INSTANTIATE_TEST_SUITE_P(Executions, TTestTabletWriteGenerationOneBatch, testing::ValuesIn(OneBatchExecutionPlans), FormatParameter);

////////////////////////////////////////////////////////////////////////////////

// Consider a scenario with two generations of the one-batch transaction. Assume that
// Wnc stands for write of generation n of batch c, Ak stands for mutation application up to (k+1)-st mutation and
// R stands for recovery.
constexpr TStringBuf TwoBatchExecutionPlans[] = {
    "W0a_W1b_W1a_A2",
    "W0a_W1b_W1a_A1_R_A2",
    "W0a_W1b_W1a_A0_R_A2",
    "W0a_W1b_W1a_R_A2",
    "W0a_W1b_R_A1_W1a_A2",
    "W0a_R_A0_W1b_W1a_A2",
    "W0a_R_A0_W1b_R_A1_W1a_A2",
    // In plans below W0b is late as the next generation arrives before it.
    "W0a_W1b_W0b_W1a_A2",
    "W0a_W1b_W0b_W1a_A1_R_A2",
    "W0a_W1b_W0b_W1a_A0_R_A2",
    "W0a_W1b_W0b_W1a_R_A2",
    "W0a_W1b_W0b_R_A1_W1a_A2",
    "W0a_W1b_R_A1_W0b_W1a_A2",
    "W0a_R_A0_W1b_W0b_W1a_A2",
    "W0a_R_A0_W1b_W0b_R_A1_W1a_A2",
};

using TTestTabletWriteGenerationTwoBatch = TTestTabletWriteGenerationSimple;

TEST_P(TTestTabletWriteGenerationTwoBatch, TwoBatchRetry)
{
    const auto& executionPlan = GetParam();
    auto txId = MakeTabletTransactionId(TTimestamp(0x10));

    THashMap<TStringBuf, std::function<void()>> actions;

    actions["W0a"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ 1))
            .ThrowOnError();
    };
    actions["W0b"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ FinalTransactionSignature - 1))
            .ThrowOnError();
    };
    actions["W1a"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(0, 42)}, /*signature*/ 1, /*generation*/ 1))
            .ThrowOnError();
    };
    actions["W1b"] = [&] {
        WaitFor(WriteUnversionedRows(txId, {BuildRow(1, 42)}, /*signature*/ FinalTransactionSignature - 1, /*generation*/ 1))
            .ThrowOnError();
    };
    actions["A0"] = [&] {
        HydraManager()->ApplyUpTo(1);
    };
    actions["A1"] = [&] {
        HydraManager()->ApplyUpTo(2);
    };
    actions["A2"] = [&] {
        HydraManager()->ApplyUpTo(3);
    };
    actions["R"] = [&] {
        HydraManager()->SaveLoad();
    };

    // At least one of the rows should be locked until the end of our transaction. Opposite to previous test,
    // we simply test if tablet's store manager has locks. We cannot run a concurrent transaction here
    // since the current implementation of tablet write manager does not reset acquired row locks upon row
    // lock conflict, so we may affect the store state by such check.
    auto validateRowLock = [&] {
        EXPECT_TRUE(HasActiveStoreLocks());
    };

    for (const auto& token : StringSplitter(executionPlan).Split('_')) {
        const auto& action = GetOrCrash(actions, token);
        action();
        if (HydraManager()->IsActiveLeader()) {
            validateRowLock();
        }
    }

    auto asyncCommit = PrepareAndCommitTransaction(txId, true, 0x20);

    EXPECT_EQ(2, HydraManager()->GetPendingMutationCount());
    HydraManager()->ApplyAll();

    asyncCommit
        .Get()
        .ThrowOnError();

    EXPECT_EQ(
        ToString(BuildVersionedRow(0, {{0x20, 42}})),
        ToString(VersionedLookupRow(BuildRow(0))));
    EXPECT_EQ(
        ToString(BuildVersionedRow(1, {{0x20, 42}})),
        ToString(VersionedLookupRow(BuildRow(1))));

    ExpectFullyUnlocked();
}

INSTANTIATE_TEST_SUITE_P(Executions, TTestTabletWriteGenerationTwoBatch, testing::ValuesIn(TwoBatchExecutionPlans), FormatParameter);

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NTabletNode
