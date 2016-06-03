#pragma once

#include "public.h"

#include <yt/server/cell_node/public.h>

#include <yt/server/hydra/entity_map.h>

#include <yt/server/tablet_node/tablet_manager.pb.h>

#include <yt/ytlib/chunk_client/chunk_meta.pb.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/ytlib/tablet_client/public.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/misc/small_vector.h>

#include <yt/core/ytree/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TTabletManager
    : public TRefCounted
{
public:
    TTabletManager(
        TTabletManagerConfigPtr config,
        TTabletSlotPtr slot,
        NCellNode::TBootstrap* bootstrap);
    ~TTabletManager();

    void Initialize();


    void Read(
        TTabletSnapshotPtr tabletSnapshot,
        TTimestamp timestamp,
        const TWorkloadDescriptor& workloadDescriptor,
        NTabletClient::TWireProtocolReader* reader,
        NTabletClient::TWireProtocolWriter* writer);

    void Write(
        TTabletSnapshotPtr tabletSnapshot,
        const TTransactionId& transactionId,
        NTransactionClient::TTimestamp transactionStartTimestamp,
        TDuration transactionTimeout,
        TTransactionSignature signature,
        NTabletClient::TWireProtocolReader* reader,
        TFuture<void>* commitResult);


    void ScheduleStoreRotation(TTablet* tablet);

    NYTree::IYPathServicePtr GetOrchidService();


    DECLARE_ENTITY_MAP_ACCESSORS(Tablet, TTablet);
    TTablet* GetTabletOrThrow(const TTabletId& id);

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;

};

DEFINE_REFCOUNTED_TYPE(TTabletManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
