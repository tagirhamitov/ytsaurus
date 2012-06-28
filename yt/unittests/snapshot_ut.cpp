#include "stdafx.h"

#include <ytlib/meta_state/common.h>
#include <ytlib/meta_state/config.h>
#include <ytlib/meta_state/snapshot.h>
#include <ytlib/meta_state/snapshot_store.h>

#include <ytlib/ytree/convert.h>

#include <util/random/random.h>
#include <util/system/tempfile.h>

#include <contrib/testing/framework.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TSnapshotTest
    : public ::testing::Test
{
protected:
    THolder<TTempFile> TemporaryFile;

    virtual void SetUp()
    {
        TemporaryFile.Reset(new TTempFile(GenerateRandomFileName("Snapshot")));
    }

    virtual void TearDown()
    {
        TemporaryFile.Reset(NULL);
    }
};

TEST_F(TSnapshotTest, EmptySnapshot)
{
    // TODO: Add checksums.
    ASSERT_NO_THROW({
        TSnapshotWriterPtr writer = New<TSnapshotWriter>(
            TemporaryFile->Name(),
            0,
            true);
        writer->Open(NonexistingPrevRecordCount, TEpoch());
        writer->Close();
    });

    ASSERT_NO_THROW({
        TSnapshotReaderPtr reader = New<TSnapshotReader>(
            TemporaryFile->Name(),
            0,
            true);
        reader->Open();
    });
}

TEST_F(TSnapshotTest, WriteAndThenRead)
{
    // TODO: Add checksums.
    const i32 recordCount = 1024;
    const TEpoch epoch(1, 2);

    TSnapshotWriterPtr writer = New<TSnapshotWriter>(
        TemporaryFile->Name(),
        0,
        true);
    writer->Open(NonexistingPrevRecordCount, epoch);
    auto* outputStream = writer->GetStream();

    for (i32 i = 0; i < recordCount; ++i) {
        outputStream->Write(&i, sizeof(i32));
    }

    writer->Close();
    writer.Reset();

    TSnapshotReaderPtr reader = New<TSnapshotReader>(
        TemporaryFile->Name(),
        0,
        true);
    reader->Open();

    EXPECT_EQ(reader->GetPrevRecordCount(), NonexistingPrevRecordCount);
    EXPECT_EQ(reader->GetEpoch(), epoch);

    auto* inputStream = reader->GetStream();

    for (i32 i = 0; i < recordCount; ++i) {
        i32 data;
        i32 bytesRead = inputStream->Load(&data, sizeof(i32));

        EXPECT_EQ(static_cast<i32>(sizeof(i32)), bytesRead);
        EXPECT_EQ(i, data);
    }

    reader.Reset();
}

////////////////////////////////////////////////////////////////////////////////

TEST_F(TSnapshotTest, SnapshotStore)
{
    TSnapshotStoreConfigPtr config = New<TSnapshotStoreConfig>();
    config->Load(NYTree::ConvertToNode(NYTree::TYsonString("{path=snapshot_store}")));

    TSnapshotStorePtr store = New<TSnapshotStore>(config);
    store->Start();

    EXPECT_IS_FALSE(store->GetReader(1).IsOK());

    auto writer = store->GetWriter(2);
    writer->Open(1, TEpoch());
    TOutputStream* output = writer->GetStream();
    std::vector<char> data(10, 42);
    output->Write(&*data.begin(), data.size());
    writer->Close();

    auto readerResult = store->GetReader(2);
    ASSERT_TRUE(readerResult.IsOK());
    auto reader = readerResult.Value();
    reader->Open();
    EXPECT_EQ(1, reader->GetPrevRecordCount());
    EXPECT_EQ(TEpoch(), reader->GetEpoch());

    EXPECT_IS_FALSE(store->GetReader(3).IsOK());

    // TODO(ignat): add more tests
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT

