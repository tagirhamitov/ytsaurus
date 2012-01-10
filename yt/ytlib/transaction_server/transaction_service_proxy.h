#pragma once

#include "common.h"
#include "transaction_service.pb.h"

#include <ytlib/rpc/service.h>
#include <ytlib/rpc/client.h>

namespace NYT {
namespace NTransactionServer {

////////////////////////////////////////////////////////////////////////////////

class TTransactionServiceProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TTransactionServiceProxy> TPtr;

    static Stroka GetServiceName()
    {
        return "TransactonService";
    }

    DECLARE_ENUM(EErrorCode,
        ((NoSuchTransaction)(1))
    );

    TTransactionServiceProxy(NRpc::IChannel* channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NProto, StartTransaction);
    DEFINE_RPC_PROXY_METHOD(NProto, CommitTransaction);
    DEFINE_RPC_PROXY_METHOD(NProto, AbortTransaction);
    DEFINE_RPC_PROXY_METHOD(NProto, RenewTransactionLease);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT
