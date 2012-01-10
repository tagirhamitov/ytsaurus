#include "stdafx.h"
#include "file_commands.h"

#include <ytlib/file_client/file_reader.h>
#include <ytlib/file_client/file_writer.h>

namespace NYT {
namespace NDriver {

using namespace NYTree;
using namespace NFileClient;

////////////////////////////////////////////////////////////////////////////////

void TDownloadCommand::DoExecute(TDownloadRequest* request)
{
    auto config = DriverImpl->GetConfig()->FileReader;

    auto reader = New<TFileReader>(
        ~config,
        DriverImpl->GetMasterChannel(),
        DriverImpl->GetCurrentTransaction(),
        DriverImpl->GetBlockCache(),
        request->Path);

    // TODO(babenko): use FileName and Executable values

    auto output = DriverImpl->CreateOutputStream(ToStreamSpec(request->Stream));

    while (true) {
        auto block = reader->Read();
        if (!block) {
            break;
        }
        output->Write(block.Begin(), block.Size());
    }
}

////////////////////////////////////////////////////////////////////////////////

void TUploadCommand::DoExecute(TUploadRequest* request)
{
    auto config = DriverImpl->GetConfig()->FileWriter;

    auto writer = New<TFileWriter>(
        ~config,
        DriverImpl->GetMasterChannel(),
        DriverImpl->GetCurrentTransaction(),
        DriverImpl->GetTransactionManager(),
        request->Path);

    auto input = DriverImpl->CreateInputStream(ToStreamSpec(request->Stream));
    
    TBlob buffer(config->BlockSize);
    while (true) {
        size_t bytesRead = input->Read(buffer.begin(), buffer.size());
        if (bytesRead == 0)
            break;
        TRef block(buffer.begin(), bytesRead);
        writer->Write(block);
    }

    writer->Close();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
