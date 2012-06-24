#include "executor.h"
#include "cypress_executors.h"
#include "transaction_executors.h"
#include "file_executors.h"
#include "table_executors.h"
#include "scheduler_executors.h"

#include <ytlib/logging/log_manager.h>

#include <ytlib/profiling/profiling_manager.h>

#include <ytlib/misc/delayed_invoker.h>

#include <ytlib/driver/driver.h>
#include <ytlib/driver/config.h>

#include <ytlib/bus/tcp_dispatcher.h>

#include <ytlib/ytree/serialize.h>
#include <ytlib/ytree/yson_parser.h>

#include <ytlib/exec_agent/config.h>

#include <ytlib/misc/errortrace.h>
#include <ytlib/misc/thread.h>

#include <util/stream/pipe.h>

#include <build.h>

namespace NYT {

using namespace NDriver;
using namespace NYTree;

/////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = DriverLogger;

/////////////////////////////////////////////////////////////////////////////

class TDriverProgram
{
public:
    TDriverProgram()
        : ExitCode(0)
    {
        RegisterExecutor(New<TStartTxExecutor>());
        RegisterExecutor(New<TRenewTxExecutor>());
        RegisterExecutor(New<TCommitTxExecutor>());
        RegisterExecutor(New<TAbortTxExecutor>());

        RegisterExecutor(New<TGetExecutor>());
        RegisterExecutor(New<TSetExecutor>());
        RegisterExecutor(New<TRemoveExecutor>());
        RegisterExecutor(New<TListExecutor>());
        RegisterExecutor(New<TCreateExecutor>());
        RegisterExecutor(New<TLockExecutor>());

        RegisterExecutor(New<TDownloadExecutor>());
        RegisterExecutor(New<TUploadExecutor>());

        RegisterExecutor(New<TReadExecutor>());
        RegisterExecutor(New<TWriteExecutor>());

        RegisterExecutor(New<TMapExecutor>());
        RegisterExecutor(New<TMergeExecutor>());
        RegisterExecutor(New<TSortExecutor>());
        RegisterExecutor(New<TEraseExecutor>());
        RegisterExecutor(New<TReduceExecutor>());
        RegisterExecutor(New<TAbortOpExecutor>());
        RegisterExecutor(New<TTrackOpExecutor>());
    }

    int Main(int argc, const char* argv[])
    {
        NYT::SetupErrorHandler();
        NYT::NThread::SetCurrentThreadName("Driver");

        try {
            if (argc < 2) {
                PrintAllCommands();
                ythrow yexception() << "Not enough arguments";
            }
            
            Stroka commandName = Stroka(argv[1]);

            if (commandName == "--help") {
                PrintAllCommands();
                return 0;
            }

            if (commandName == "--version") {
                PrintVersion();
                return 0;
            }

            if (commandName == "--config-template") {
                TYsonWriter writer(&Cout, EYsonFormat::Pretty);
                New<TExecutorConfig>()->Save(&writer);
                return 0;
            }

            auto Executor = GetExecutor(commandName);

            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) {
                args.push_back(std::string(argv[i]));
            }

            Executor->Execute(args);
        } catch (const std::exception& ex) {
            Cerr << "ERROR: " << ex.what() << Endl;
            ExitCode = 1;
        }

        // TODO: refactor system shutdown
        // XXX(sandello): Keep in sync with server/main.cpp, driver/main.cpp and utmain.cpp.
        NLog::TLogManager::Get()->Shutdown();
        NBus::TTcpDispatcher::Get()->Shutdown();
        NProfiling::TProfilingManager::Get()->Shutdown();
        TDelayedInvoker::Shutdown();

        return ExitCode;
    }

private:
    int ExitCode;
    yhash_map<Stroka, TExecutorPtr> Executors;

    void PrintAllCommands()
    {
        printf("Available commands:\n");
        FOREACH (const auto& pair, GetSortedIterators(Executors)) {
            printf("  %s\n", ~pair->first);
        }
    }

    void PrintVersion()
    {
        printf("%s\n", YT_VERSION);
    }

    void RegisterExecutor(TExecutorPtr executor)
    {
        auto name = executor->GetCommandName();
        YCHECK(Executors.insert(MakePair(name, executor)).second);
    }

    TExecutorPtr GetExecutor(const Stroka& commandName)
    {
        auto it = Executors.find(commandName);
        if (it == Executors.end()) {
            ythrow yexception() << Sprintf("Unknown command %s", ~commandName.Quote());
        }
        return it->second;
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

int main(int argc, const char* argv[])
{
    NYT::TDriverProgram program;
    return program.Main(argc, argv);
}

