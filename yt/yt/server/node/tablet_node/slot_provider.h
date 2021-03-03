#include "public.h"

#include <yt/server/node/cluster_node/public.h>

#include <yt/server/lib/cellar_agent/public.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

NCellarAgent::ICellarOccupierProviderPtr CreateTabletSlotOccupierProvider(
    TTabletNodeConfigPtr config,
    NClusterNode::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode::NYT
