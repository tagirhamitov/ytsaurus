#pragma once

#include "public.h"
#include "lock.h"

#include <ytlib/cypress_client/public.h>

#include <server/cell_master/public.h>

#include <server/transaction_server/public.h>

#include <server/object_server/public.h>

namespace NYT {
namespace NCypressServer {

////////////////////////////////////////////////////////////////////////////////

//! Provides a common interface for all persistent nodes.
struct ICypressNode
    : private TNonCopyable
{
    virtual ~ICypressNode()
    { }

    //! Returns node type.
    virtual NObjectClient::EObjectType GetObjectType() const = 0;

    //! Saves the node into the snapshot stream.
    virtual void Save(TOutputStream* output) const = 0;
    
    //! Loads the node from the snapshot stream.
    virtual void Load(const NCellMaster::TLoadContext& context, TInputStream* input) = 0;

    //! Returns the composite (versioned) id of the node.
    virtual const TVersionedNodeId& GetId() const = 0;

    //! Gets the lock mode.
    virtual ELockMode GetLockMode() const = 0;
    //! Sets the lock mode.
    virtual void SetLockMode(ELockMode mode) = 0;

    //! Returns the trunk node, i.e. for a node with id |(objectId, transactionId)| returns
    //! the node with id |(objectId, NullTransactionId)|.
    virtual ICypressNode* GetTrunkNode() const = 0;
    //! Used internally to set the trunk node during branching.
    virtual void SetTrunkNode(ICypressNode* trunkNode) = 0;

    //! Gets the parent node id.
    virtual TNodeId GetParentId() const = 0;
    //! Sets the parent node id.
    virtual void SetParentId(TNodeId value) = 0;

    typedef yhash_map<NTransactionServer::TTransaction*, TLock> TLockMap;

    //! Gets an immutable reference to transaction-to-lock map.
    virtual const TLockMap& Locks() const = 0;
    //! Gets a mutable reference to transaction-to-lock map.
    virtual TLockMap& Locks() = 0;
    
    virtual TInstant GetCreationTime() const = 0;
    virtual void SetCreationTime(TInstant value) = 0;

    //! Increments the reference counter, returns the incremented value.
    virtual i32 RefObject() = 0;
    //! Decrements the reference counter, returns the decremented value.
    virtual i32 UnrefObject() = 0;
    //! Returns the current reference counter value.
    virtual i32 GetObjectRefCounter() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

// TObjectIdTraits and GetObjectId specializations.

namespace NYT {
namespace NObjectServer {

template <>
struct TObjectIdTraits<NCypressServer::ICypressNode*, void>
{
    typedef TVersionedObjectId TId;
};

template <class T>
TVersionedObjectId GetObjectId(
    T object,
    typename NMpl::TEnableIf< NMpl::TIsConvertible<T, NCypressServer::ICypressNode*>, void* >::TType = NULL)
{
    return object ? object->GetId() : TVersionedObjectId(NullObjectId, NullTransactionId);
}

} // namespace NObjectServer
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////
