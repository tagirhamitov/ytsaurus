#pragma once

#include "public.h"

#include <core/rpc/client.h>

#include <ytlib/tablet_client/tablet_service.pb.h>

namespace NYT {
namespace NTabletClient {

////////////////////////////////////////////////////////////////////////////////

class TTabletServiceProxy
    : public NRpc::TProxyBase
{
public:
    static Stroka GetServiceName()
    {
        return "TabletService";
    }

    explicit TTabletServiceProxy(NRpc::IChannelPtr channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NProto, Write);
    DEFINE_RPC_PROXY_METHOD(NProto, Lookup);
    DEFINE_RPC_PROXY_METHOD(NProto, Delete);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletClient
} // namespace NYT

