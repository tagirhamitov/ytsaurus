#pragma once

#include "public.h"

// TODO(babenko): replace with public.h
#include <core/actions/bind.h>
#include <core/actions/invoker.h>

#include <core/concurrency/periodic_executor.h>

#include <ytlib/chunk_client/data_node_service_proxy.h>

#include <server/cell_node/public.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

class TPeerBlockUpdater
    : public TRefCounted
{
public:
    TPeerBlockUpdater(
        TDataNodeConfigPtr config,
        NCellNode::TBootstrap* bootstrap);

    void Start();
    void Stop();

private:
    void Update();

    typedef NChunkClient::TDataNodeServiceProxy TProxy;

    TDataNodeConfigPtr Config;
    NCellNode::TBootstrap* Bootstrap;
    NConcurrency::TPeriodicExecutorPtr PeriodicExecutor;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
