#include "chunk_replica.h"
#include "chunk.h"

#include <yt/server/node_tracker_server/node.h>

#include <yt/ytlib/chunk_client/chunk_replica.h>
#include <yt/ytlib/chunk_client/public.h>

#include <yt/core/misc/string.h>

namespace NYT {
namespace NChunkServer {

using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(TNodePtrWithIndexes value)
{
    if (value.GetReplicaIndex() == GenericChunkReplicaIndex) {
        return Format("%v@%v",
            value.GetPtr()->GetDefaultAddress(),
            value.GetMediumIndex());
    } else {
        return Format("%v/%v@%v",
            value.GetPtr()->GetDefaultAddress(),
            value.GetReplicaIndex(),
            value.GetMediumIndex());
    }
}

Stroka ToString(TChunkPtrWithIndexes value)
{
    auto* chunk = value.GetPtr();
    int replicaIndex = value.GetReplicaIndex();
    if (chunk->IsJournal()) {
        return Format("%v/%v",
            chunk->GetId(),
            EJournalReplicaType(replicaIndex));
    } else if (replicaIndex != GenericChunkReplicaIndex) {
        return Format("%v/%v@%v",
            chunk->GetId(),
            replicaIndex,
            value.GetMediumIndex());
    } else {
        return Format("%v@%v", chunk->GetId(), value.GetMediumIndex());
    }
}

void ToProto(ui32* protoValue, TNodePtrWithIndexes value)
{
    NChunkClient::TChunkReplica clientReplica(
        value.GetPtr()->GetId(),
        value.GetReplicaIndex(),
        value.GetMediumIndex());
    NChunkClient::ToProto(protoValue, clientReplica);
}

TChunkId EncodeChunkId(TChunkPtrWithIndexes chunkWithIndexes)
{
    auto* chunk = chunkWithIndexes.GetPtr();
    return chunk->IsErasure()
        ? ErasurePartIdFromChunkId(chunk->GetId(), chunkWithIndexes.GetReplicaIndex())
        : chunk->GetId();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
