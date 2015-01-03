#pragma once

#include <core/misc/common.h>
#include <core/misc/enum.h>

namespace NYT {
namespace NCellNode {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EMemoryConsumer,
    (Footprint)
    (BlockCache)
    (ChunkMeta)
    (Job)
    (Tablet)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellNode
} // namespace NYT
