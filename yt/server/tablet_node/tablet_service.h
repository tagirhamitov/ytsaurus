#pragma once

#include "public.h"

#include <ytlib/tablet_client/tablet_service.pb.h>

#include <server/hydra/hydra_service.h>

#include <server/cell_node/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TTabletService
    : public NHydra::THydraServiceBase
{
public:
    TTabletService(
        TTabletSlot* slot,
        NCellNode::TBootstrap* bootstrap);

    void Start();
    void Stop();

private:
    typedef TTabletService TThis;

    TTabletSlot* Slot;
    NCellNode::TBootstrap* Bootstrap;

    DECLARE_RPC_SERVICE_METHOD(NTabletClient::NProto, Write);
    DECLARE_RPC_SERVICE_METHOD(NTabletClient::NProto, Lookup);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
