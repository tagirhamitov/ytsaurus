#pragma once

#include "common.h"
#include "node_proxy.h"
#include "node_detail.h"
#include "cypress_ypath.pb.h"

#include <ytlib/ytree/ytree.h>
#include <ytlib/ytree/ypath_service.h>
#include <ytlib/ytree/ypath_detail.h>
#include <ytlib/ytree/node_detail.h>
#include <ytlib/ytree/serialize.h>
#include <ytlib/ytree/ephemeral.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/object_server/object_detail.h>

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

class TNodeFactory
    : public NYTree::INodeFactory
{
public:
    TNodeFactory(
        TCypressManager* cypressManager,
        const TTransactionId& transactionId);
    ~TNodeFactory();

    virtual NYTree::IStringNode::TPtr CreateString();
    virtual NYTree::IInt64Node::TPtr CreateInt64();
    virtual NYTree::IDoubleNode::TPtr CreateDouble();
    virtual NYTree::IMapNode::TPtr CreateMap();
    virtual NYTree::IListNode::TPtr CreateList();
    virtual NYTree::IEntityNode::TPtr CreateEntity();

private:
    const TCypressManager::TPtr CypressManager;
    const TTransactionId TransactionId;
    yvector<TNodeId> CreatedNodeIds;

    ICypressNodeProxy::TPtr DoCreate(EObjectType type);

};

////////////////////////////////////////////////////////////////////////////////

template <class IBase, class TImpl>
class TCypressNodeProxyBase
    : public NYTree::TNodeBase
    , public NObjectServer::TObjectProxyBase
    , public ICypressNodeProxy
    , public virtual IBase
{
public:
    typedef TIntrusivePtr<TCypressNodeProxyBase> TPtr;

    TCypressNodeProxyBase(
        INodeTypeHandler* typeHandler,
        TCypressManager* cypressManager,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : NObjectServer::TObjectProxyBase(
            cypressManager->GetObjectManager(),
            nodeId,
            CypressLogger.GetCategory())
        , TypeHandler(typeHandler)
        , CypressManager(cypressManager)
        , ObjectManager(cypressManager->GetObjectManager())
        , TransactionId(transactionId)
        , NodeId(nodeId)
        , Locked(false)
    {
        YASSERT(typeHandler);
        YASSERT(cypressManager);
    }

    NYTree::INodeFactory::TPtr CreateFactory() const
    {
        return New<TNodeFactory>(~CypressManager, TransactionId);
    }

    virtual TTransactionId GetTransactionId() const
    {
        return TransactionId;
    }

    virtual TNodeId GetId() const
    {
        return NodeId;
    }


    virtual NYTree::ENodeType GetType() const
    {
        return TypeHandler->GetNodeType();
    }


    virtual const ICypressNode& GetImpl() const
    {
        return this->GetTypedImpl();
    }

    virtual ICypressNode& GetImplForUpdate()
    {
        return this->GetTypedImplForUpdate();
    }


    virtual NYTree::ICompositeNode::TPtr GetParent() const
    {
        auto nodeId = GetImpl().GetParentId();
        return nodeId == NullObjectId ? NULL : GetProxy(nodeId)->AsComposite();
    }

    virtual void SetParent(NYTree::ICompositeNode* parent)
    {
        GetImplForUpdate().SetParentId(
            parent
            ? ToProxy(parent)->GetId()
            : NullObjectId);
    }


    virtual bool IsWriteRequest(NRpc::IServiceContext* context) const
    {
        DECLARE_YPATH_SERVICE_WRITE_METHOD(Lock);
        // NB: Create is not considered a write verb since it always fails here.
        return NYTree::TNodeBase::IsWriteRequest(context);
    }

protected:
    const INodeTypeHandler::TPtr TypeHandler;
    const TCypressManager::TPtr CypressManager;
    const NObjectServer::TObjectManager::TPtr ObjectManager;
    const TTransactionId TransactionId;
    const TNodeId NodeId;

    //! A cached flag that gets raised when the node is locked.
    bool Locked;


    virtual void GetSystemAttributes(yvector<TAttributeInfo>* attributes)
    {
        attributes->push_back("parent_id");
        attributes->push_back("state");
        NObjectServer::TObjectProxyBase::GetSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& name, NYTree::IYsonConsumer* consumer)
    {
        const auto& node = GetImpl();

        if (name == "parent_id") {
            BuildYsonFluently(consumer)
                .Scalar(node.GetParentId().ToString());
            return true;
        }

        if (name == "state") {
            BuildYsonFluently(consumer)
                .Scalar(CamelCaseToUnderscoreCase(node.GetState().ToString()));
            return true;
        }

        return NObjectServer::TObjectProxyBase::GetSystemAttribute(name, consumer);
    }


    virtual void DoInvoke(NRpc::IServiceContext* context)
    {
        DISPATCH_YPATH_SERVICE_METHOD(Lock);
        DISPATCH_YPATH_SERVICE_METHOD(Create);
        TNodeBase::DoInvoke(context);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, Lock)
    {
        UNUSED(request);
        UNUSED(response);

        DoLock();
        context->Reply();
    }
    
    DECLARE_RPC_SERVICE_METHOD(NProto, Create)
    {
        UNUSED(request);
        UNUSED(response);

        if (NYTree::IsFinalYPath(context->GetPath())) {
            ythrow yexception() << "Node already exists";
        }

        context->Reply(NRpc::EErrorCode::NoSuchVerb, "Verb is not supported");
    }


    const ICypressNode& GetImpl(const TNodeId& nodeId) const
    {
        return CypressManager->GetVersionedNode(nodeId, TransactionId);
    }

    ICypressNode& GetImplForUpdate(const TNodeId& nodeId) const
    {
        return CypressManager->GetVersionedNodeForUpdate(nodeId, TransactionId);
    }


    const TImpl& GetTypedImpl() const
    {
        return dynamic_cast<const TImpl&>(GetImpl(NodeId));
    }

    TImpl& GetTypedImplForUpdate()
    {
        return dynamic_cast<TImpl&>(GetImplForUpdate(NodeId));
    }


    ICypressNodeProxy::TPtr GetProxy(const TNodeId& nodeId) const
    {
        YASSERT(nodeId != NullObjectId);
        return CypressManager->GetVersionedNodeProxy(nodeId, TransactionId);
    }

    static ICypressNodeProxy* ToProxy(INode* node)
    {
        YASSERT(node);
        return &dynamic_cast<ICypressNodeProxy&>(*node);
    }

    static const ICypressNodeProxy* ToProxy(const INode* node)
    {
        YASSERT(node);
        return &dynamic_cast<const ICypressNodeProxy&>(*node);
    }


    void LockIfNeeded()
    {
        if (!Locked && CypressManager->IsLockNeeded(NodeId, TransactionId)) {
            if (TransactionId == NullTransactionId) {
                ythrow yexception() << "The requested operation requires a lock but no current transaction is given";
            }
            DoLock();
        }
    }

    void DoLock()
    {
        if (TransactionId == NullTransactionId) {
            ythrow yexception() << "Cannot lock a node outside of a transaction";
        }

        CypressManager->LockVersionedNode(NodeId, TransactionId);

        // Set the flag to speedup further checks.
        Locked = true;
    }


    void AttachChild(ICypressNode& child)
    {
        child.SetParentId(NodeId);
        ObjectManager->RefObject(child.GetId().ObjectId);
    }

    void DetachChild(ICypressNode& child)
    {
        child.SetParentId(NullObjectId);
        ObjectManager->UnrefObject(child.GetId().ObjectId);
    }


    virtual const NObjectServer::TAttributeSet* FindAttributes()
    {
        auto id = GetImpl(NodeId).GetId();
        return ObjectManager->FindAttributes(id);
    }

    virtual NObjectServer::TAttributeSet* FindAttributesForUpdate()
    {
        auto id = GetImplForUpdate(NodeId).GetId();
        return ObjectManager->FindAttributesForUpdate(id);
    }

    virtual NObjectServer::TAttributeSet* GetAttributesForUpdate()
    {
        auto id = GetImplForUpdate(NodeId).GetId();
        auto* attributes = ObjectManager->FindAttributesForUpdate(id);
        if (!attributes) {
            attributes = ObjectManager->CreateAttributes(id);
        }
        return attributes;
    }

    virtual void RemoveAttributes()
    {
        auto id = GetImplForUpdate(NodeId).GetId();
        if (ObjectManager->FindAttributes(id)) {
            ObjectManager->RemoveAttributes(id);
        }
    }

};

//////////////////////////////////////////////////////////////////////////////// 

template <class TValue, class IBase, class TImpl>
class TScalarNodeProxy
    : public TCypressNodeProxyBase<IBase, TImpl>
{
public:
    TScalarNodeProxy(
        INodeTypeHandler* typeHandler,
        TCypressManager* cypressManager,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : TCypressNodeProxyBase<IBase, TImpl>(
            typeHandler,
            cypressManager,
            transactionId,
            nodeId)
    { }

    virtual TValue GetValue() const
    {
        return this->GetTypedImpl().Value();
    }

    virtual void SetValue(const TValue& value)
    {
        this->LockIfNeeded();
        this->GetTypedImplForUpdate().Value() = value;
    }
};

//////////////////////////////////////////////////////////////////////////////// 

#define DECLARE_SCALAR_TYPE(key, type) \
    class T##key##NodeProxy \
        : public TScalarNodeProxy<type, NYTree::I##key##Node, T##key##Node> \
    { \
        YTREE_NODE_TYPE_OVERRIDES(key) \
    \
    public: \
        T##key##NodeProxy( \
            INodeTypeHandler* typeHandler, \
            TCypressManager* cypressManager, \
            const TTransactionId& transactionId, \
            const TNodeId& id) \
            : TScalarNodeProxy<type, NYTree::I##key##Node, T##key##Node>( \
                typeHandler, \
                cypressManager, \
                transactionId, \
                id) \
        { } \
    }; \
    \
    template <> \
    inline ICypressNodeProxy::TPtr TScalarNodeTypeHandler<type>::GetProxy( \
        const ICypressNode& node, \
        const TTransactionId& transactionId) \
    { \
        return New<T##key##NodeProxy>( \
            this, \
            ~CypressManager, \
            transactionId, \
            node.GetId().ObjectId); \
    }

DECLARE_SCALAR_TYPE(String, Stroka)
DECLARE_SCALAR_TYPE(Int64, i64)
DECLARE_SCALAR_TYPE(Double, double)

#undef DECLARE_SCALAR_TYPE

////////////////////////////////////////////////////////////////////////////////

template <class IBase, class TImpl>
class TCompositeNodeProxyBase
    : public TCypressNodeProxyBase<IBase, TImpl>
{
public:
    virtual TIntrusivePtr<const NYTree::ICompositeNode> AsComposite() const
    {
        return this;
    }

    virtual TIntrusivePtr<NYTree::ICompositeNode> AsComposite()
    {
        return this;
    }

protected:
    typedef TCypressNodeProxyBase<IBase, TImpl> TBase;

    TCompositeNodeProxyBase(
        INodeTypeHandler* typeHandler,
        TCypressManager* cypressManager,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : TBase(
            typeHandler,
            cypressManager,
            transactionId,
            nodeId)
    { }

    virtual void CreateRecursive(
        const NYTree::TYPath& path,
        NYTree::INode* value) = 0;

    virtual void DoInvoke(NRpc::IServiceContext* context)
    {
        DISPATCH_YPATH_SERVICE_METHOD(Create);
        TBase::DoInvoke(context);
    }

    virtual bool IsWriteRequest(NRpc::IServiceContext* context) const
    {
        DECLARE_YPATH_SERVICE_WRITE_METHOD(Create);
        return TBase::IsWriteRequest(context);
    }

protected:
    virtual void GetSystemAttributes(yvector<NObjectServer::TObjectProxyBase::TAttributeInfo>* attributes)
    {
        attributes->push_back("size");
        TBase::GetSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& name, NYTree::IYsonConsumer* consumer)
    {
        if (name == "size") {
            BuildYsonFluently(consumer)
                .Scalar(this->GetChildCount());
            return true;
        }

        return TBase::GetSystemAttribute(name, consumer);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, Create)
    {
        auto type = EObjectType(request->type());

        context->SetRequestInfo("Type: %s", ~type.ToString());

        if (NYTree::IsFinalYPath(context->GetPath())) {
            // This should throw an exception.
            TBase::Create(request, response, context);
            return;
        }

        NYTree::INode::TPtr manifestNode =
            request->has_manifest()
            ? NYTree::DeserializeFromYson(request->manifest())
            : NYTree::GetEphemeralNodeFactory()->CreateMap();

        if (manifestNode->GetType() != NYTree::ENodeType::Map) {
            ythrow yexception() << "Manifest must be a map";
        }

        auto handler = this->CypressManager->GetObjectManager()->FindHandler(type);
        if (!handler) {
            ythrow yexception() << "Unknown object type";
        }

        auto value = this->CypressManager->CreateDynamicNode(
            this->TransactionId,
            EObjectType(request->type()),
            ~manifestNode->AsMap());

        CreateRecursive(context->GetPath(), ~value);

        response->set_object_id(value->GetId().ToProto());

        context->Reply();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TMapNodeProxy
    : public TCompositeNodeProxyBase<NYTree::IMapNode, TMapNode>
    , public NYTree::TMapNodeMixin
{
    YTREE_NODE_TYPE_OVERRIDES(Map)

public:
    TMapNodeProxy(
        INodeTypeHandler* typeHandler,
        TCypressManager* cypressManager,
        const TTransactionId& transactionId,
        const TNodeId& nodeId);

    virtual void Clear();
    virtual int GetChildCount() const;
    virtual yvector< TPair<Stroka, NYTree::INode::TPtr> > GetChildren() const;
    virtual yvector<Stroka> GetKeys() const;
    virtual INode::TPtr FindChild(const Stroka& key) const;
    virtual bool AddChild(NYTree::INode* child, const Stroka& key);
    virtual bool RemoveChild(const Stroka& key);
    virtual void ReplaceChild(NYTree::INode* oldChild, NYTree::INode* newChild);
    virtual void RemoveChild(NYTree::INode* child);
    virtual Stroka GetChildKey(const INode* child);

protected:
    typedef TCompositeNodeProxyBase<NYTree::IMapNode, TMapNode> TBase;

    virtual void DoInvoke(NRpc::IServiceContext* context);
    virtual void CreateRecursive(const NYTree::TYPath& path, INode* value);
    virtual IYPathService::TResolveResult ResolveRecursive(const NYTree::TYPath& path, const Stroka& verb);
    virtual void SetRecursive(const NYTree::TYPath& path, TReqSet* request, TRspSet* response, TCtxSet* context);
    virtual void SetNodeRecursive(const NYTree::TYPath& path, TReqSetNode* request, TRspSetNode* response, TCtxSetNode* context);

};

////////////////////////////////////////////////////////////////////////////////

class TListNodeProxy
    : public TCompositeNodeProxyBase<NYTree::IListNode, TListNode>
    , public NYTree::TListNodeMixin
{
    YTREE_NODE_TYPE_OVERRIDES(List)

public:
    TListNodeProxy(
        INodeTypeHandler* typeHandler,
        TCypressManager* cypressManager,
        const TTransactionId& transactionId,
        const TNodeId& nodeId);

    virtual void Clear();
    virtual int GetChildCount() const;
    virtual yvector<INode::TPtr> GetChildren() const;
    virtual INode::TPtr FindChild(int index) const;
    virtual void AddChild(NYTree::INode* child, int beforeIndex = -1);
    virtual bool RemoveChild(int index);
    virtual void ReplaceChild(NYTree::INode* oldChild, NYTree::INode* newChild);
    virtual void RemoveChild(NYTree::INode* child);
    virtual int GetChildIndex(const NYTree::INode* child);

protected:
    typedef TCompositeNodeProxyBase<NYTree::IListNode, TListNode> TBase;

    virtual void CreateRecursive(const NYTree::TYPath& path, INode* value);
    virtual TResolveResult ResolveRecursive(const NYTree::TYPath& path, const Stroka& verb);
    virtual void SetRecursive(const NYTree::TYPath& path, TReqSet* request, TRspSet* response, TCtxSet* context);
    virtual void SetNodeRecursive(const NYTree::TYPath& path, TReqSetNode* request, TRspSetNode* response, TCtxSetNode* context);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
