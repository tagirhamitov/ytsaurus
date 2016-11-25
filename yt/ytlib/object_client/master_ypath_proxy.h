#pragma once

#include <yt/ytlib/object_client/master_ypath.pb.h>

#include <yt/core/ytree/ypath_proxy.h>

namespace NYT {
namespace NObjectClient {

////////////////////////////////////////////////////////////////////////////////

struct TMasterYPathProxy
{
    DEFINE_YPATH_PROXY(RPC_PROXY_DESC(Master));

    DEFINE_MUTATING_YPATH_PROXY_METHOD(NProto, CreateObject);
    DEFINE_YPATH_PROXY_METHOD(NProto, GetClusterMeta);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectClient
} // namespace NYT
