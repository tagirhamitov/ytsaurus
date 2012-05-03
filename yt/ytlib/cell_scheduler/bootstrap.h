#pragma once

#include "public.h"

#include <ytlib/actions/invoker.h>
// TODO(babenko): replace with public.h
#include <ytlib/bus/server.h>
#include <ytlib/rpc/public.h>
#include <ytlib/scheduler/public.h>
// TODO(babenko): replace with public.h
#include <ytlib/transaction_client/transaction_manager.h>

namespace NYT {
namespace NCellScheduler {

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
public:
    TBootstrap(
        const Stroka& configFileName,
        TCellSchedulerConfigPtr config);
    ~TBootstrap();

    TCellSchedulerConfigPtr GetConfig() const;
    NRpc::IChannelPtr GetMasterChannel() const;
    Stroka GetPeerAddress() const;
    IInvoker::TPtr GetControlInvoker() const;
    NTransactionClient::TTransactionManager::TPtr GetTransactionManager() const;
    NScheduler::TSchedulerPtr GetScheduler() const;

    void Run();

private:
    Stroka ConfigFileName;
    TCellSchedulerConfigPtr Config;

    IInvoker::TPtr ControlInvoker;
    NBus::IBusServer::TPtr BusServer;
    NRpc::IChannelPtr MasterChannel;
    Stroka PeerAddress;
    NTransactionClient::TTransactionManager::TPtr TransactionManager;
    NScheduler::TSchedulerPtr Scheduler;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellScheduler
} // namespace NYT
