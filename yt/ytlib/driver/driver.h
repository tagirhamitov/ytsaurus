#pragma once

#include "common.h"

#include <ytlib/misc/configurable.h>
#include <ytlib/misc/error.h>
#include <ytlib/ytree/public.h>
#include <ytlib/ytree/yson_consumer.h>
#include <ytlib/ytree/yson_writer.h>
// TODO: consider using forward declarations.
#include <ytlib/election/leader_lookup.h>
#include <ytlib/transaction_client/transaction_manager.h>
#include <ytlib/file_client/public.h>
#include <ytlib/file_client/config.h>
#include <ytlib/table_client/public.h>
#include <ytlib/table_client/config.h>
#include <ytlib/chunk_client/client_block_cache.h>

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

struct IDriverStreamProvider
{
    virtual ~IDriverStreamProvider()
    { }

    virtual TAutoPtr<TInputStream>  CreateInputStream() = 0;
    virtual TAutoPtr<TOutputStream> CreateOutputStream() = 0;
    virtual TAutoPtr<TOutputStream> CreateErrorStream() = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TDriver
{
public:
    struct TConfig
        : public TConfigurable
    {
        typedef TIntrusivePtr<TConfig> TPtr;

        NYTree::EYsonFormat OutputFormat;
        TDuration OperationWaitTimeout;
        NElection::TLeaderLookup::TConfigPtr Masters;
        NTransactionClient::TTransactionManager::TConfig::TPtr TransactionManager;
        NFileClient::TFileReaderConfigPtr FileReader;
        NFileClient::TFileWriterConfigPtr FileWriter;
        NTableClient::TChunkSequenceReaderConfigPtr ChunkSequenceReader;
        NTableClient::TChunkSequenceWriterConfigPtr ChunkSequenceWriter;
        NChunkClient::TClientBlockCacheConfigPtr BlockCache;

        TConfig()
        {
            Register("output_format", OutputFormat)
                .Default(NYTree::EYsonFormat::Text);
            Register("operation_wait_timeout", OperationWaitTimeout)
                .Default(TDuration::Seconds(3));
            Register("masters", Masters);
            Register("transaction_manager", TransactionManager)
                .DefaultNew();
            Register("file_reader", FileReader)
                .DefaultNew();
            Register("file_writer", FileWriter)
                .DefaultNew();
            Register("chunk_sequence_reader", ChunkSequenceReader)
                .DefaultNew();
            Register("chunk_sequence_writer", ChunkSequenceWriter)
                .DefaultNew();
            Register("block_cache", BlockCache)
                .DefaultNew();
        }
    };

    TDriver(
        TConfig::TPtr config,
        IDriverStreamProvider* streamProvider);
    ~TDriver();

    TError Execute(NYTree::INodePtr command);

private:
    class TImpl;

    THolder<TImpl> Impl;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

