#include "stdafx.h"
#include "node_detail.h"
#include "node_proxy_detail.h"

#include "../ytree/fluent.h"

namespace NYT {
namespace NCypress {

using namespace NYTree;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

const EObjectType::EDomain TCypressScalarTypeTraits<Stroka>::ObjectType = EObjectType::StringNode;
const char* TCypressScalarTypeTraits<Stroka>::TypeName = "string";

const EObjectType::EDomain TCypressScalarTypeTraits<i64>::ObjectType = EObjectType::Int64Node;
const char* TCypressScalarTypeTraits<i64>::TypeName = "int64";

const EObjectType::EDomain TCypressScalarTypeTraits<double>::ObjectType = EObjectType::DoubleNode;
const char* TCypressScalarTypeTraits<double>::TypeName = "double";

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

i32 hash<NYT::NCypress::TBranchedNodeId>::operator()(const NYT::NCypress::TBranchedNodeId& id) const
{
    return static_cast<i32>(THash<NYT::TGuid>()(id.NodeId)) * 497 +
        static_cast<i32>(THash<NYT::TGuid>()(id.TransactionId));
}

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

TCypressNodeBase::TCypressNodeBase(const TBranchedNodeId& id, EObjectType objectType)
    : ParentId_(NullNodeId)
    , AttributesId_(NullNodeId)
    , State_(ENodeState::Uncommitted)
    , Id(id)
    , ObjectType(objectType)
    , RefCounter(0)
{ }

TCypressNodeBase::TCypressNodeBase(const TBranchedNodeId& id, const TCypressNodeBase& other)
    : ParentId_(other.ParentId_)
    , AttributesId_(other.AttributesId_)
    , State_(other.State_)
    , Id(id)
    , ObjectType(other.ObjectType)
    , RefCounter(0)
{ }

EObjectType TCypressNodeBase::GetObjectType() const
{
    return ObjectType;
}

TBranchedNodeId TCypressNodeBase::GetId() const
{
    return Id;
}

i32 TCypressNodeBase::RefObject()
{
    YASSERT(State_ != ENodeState::Branched);
    return ++RefCounter;
}

i32 TCypressNodeBase::UnrefObject()
{
    YASSERT(State_ != ENodeState::Branched);
    return --RefCounter;
}

i32 TCypressNodeBase::GetObjectRefCounter() const
{
    return RefCounter;
}

void TCypressNodeBase::Save(TOutputStream* output) const
{
    ::Save(output, RefCounter);
    SaveSet(output, LockIds_);
    ::Save(output, ParentId_);
    ::Save(output, AttributesId_);
    ::Save(output, State_);
}

void TCypressNodeBase::Load(TInputStream* input)
{
    ::Load(input, RefCounter);
    LoadSet(input, LockIds_);
    ::Load(input, ParentId_);
    ::Load(input, AttributesId_);
    ::Load(input, State_);
}

////////////////////////////////////////////////////////////////////////////////

TMapNode::TMapNode(const TBranchedNodeId& id, EObjectType objectType)
    : TCypressNodeBase(id, objectType)
{ }

TMapNode::TMapNode(const TBranchedNodeId& id, const TMapNode& other)
    : TCypressNodeBase(id, other)
{
    NameToChild_ = other.NameToChild_;
    ChildToName_ = other.ChildToName_;
}

TAutoPtr<ICypressNode> TMapNode::Clone() const
{
    return new TMapNode(Id, *this);
}

void TMapNode::Save(TOutputStream* output) const
{
    TCypressNodeBase::Save(output);
    SaveMap(output, ChildToName());
}

void TMapNode::Load(TInputStream* input)
{
    TCypressNodeBase::Load(input);
    ::Load(input, ChildToName());
    FOREACH(const auto& pair, ChildToName()) {
        NameToChild().insert(MakePair(pair.Second(), pair.First()));
    }
}

////////////////////////////////////////////////////////////////////////////////

TMapNodeTypeHandler::TMapNodeTypeHandler(TCypressManager* cypressManager)
    : TCypressNodeTypeHandlerBase<TMapNode>(cypressManager)
{
    RegisterGetter("size", FromMethod(&TThis::GetSize));
}

EObjectType TMapNodeTypeHandler::GetObjectType()
{
    return EObjectType::MapNode;
}

ENodeType TMapNodeTypeHandler::GetNodeType()
{
    return ENodeType::Map;
}

Stroka TMapNodeTypeHandler::GetTypeName()
{
    return "map";
}

void TMapNodeTypeHandler::DoDestroy(TMapNode& node)
{
    // Drop references to the children.
    FOREACH (const auto& pair, node.NameToChild()) {
        CypressManager->GetObjectManager()->UnrefObject(pair.second);
    }
}

void TMapNodeTypeHandler::DoBranch(
    const TMapNode& committedNode,
    TMapNode& branchedNode)
{
    UNUSED(branchedNode);

    // Reference all children.
    FOREACH (const auto& pair, committedNode.NameToChild()) {
        CypressManager->GetObjectManager()->RefObject(pair.second);
    }
}

void TMapNodeTypeHandler::DoMerge(
    TMapNode& committedNode,
    TMapNode& branchedNode )
{
    // Drop all references held by the originator.
    FOREACH (const auto& pair, committedNode.NameToChild()) {
        CypressManager->GetObjectManager()->UnrefObject(pair.second);
    }

    // Replace the child list with the branched copy.
    committedNode.NameToChild().swap(branchedNode.NameToChild());
    committedNode.ChildToName().swap(branchedNode.ChildToName());
}

void TMapNodeTypeHandler::GetSize(const TGetAttributeParam& param)
{
    BuildYsonFluently(param.Consumer)
        .Scalar(param.Node->NameToChild().ysize());
}

////////////////////////////////////////////////////////////////////////////////

TListNode::TListNode(const TBranchedNodeId& id, EObjectType objectType)
    : TCypressNodeBase(id, objectType)
{ }

TListNode::TListNode(const TBranchedNodeId& id, const TListNode& other)
    : TCypressNodeBase(id, other)
{
    IndexToChild_ = other.IndexToChild_;
    ChildToIndex_ = other.ChildToIndex_;
}

TAutoPtr<ICypressNode> TListNode::Clone() const
{
    return new TListNode(Id, *this);
}

ICypressNodeProxy::TPtr TMapNodeTypeHandler::GetProxy(
    const ICypressNode& node,
    const TTransactionId& transactionId)
{
    return New<TMapNodeProxy>(
        this,
        ~CypressManager,
        transactionId,
        node.GetId().NodeId);
}

void TListNode::Save(TOutputStream* output) const
{
    TCypressNodeBase::Save(output);
    ::Save(output, IndexToChild());
}

void TListNode::Load(TInputStream* input)
{
    TCypressNodeBase::Load(input);
    ::Load(input, IndexToChild());
    for (int i = 0; i < IndexToChild().ysize(); ++i) {
        ChildToIndex()[IndexToChild()[i]] = i;
    }
}

////////////////////////////////////////////////////////////////////////////////

TListNodeTypeHandler::TListNodeTypeHandler(TCypressManager* cypressManager)
    : TCypressNodeTypeHandlerBase<TListNode>(cypressManager)
{
    RegisterGetter("size", FromMethod(&TThis::GetSize));
}

EObjectType TListNodeTypeHandler::GetObjectType()
{
    return EObjectType::ListNode;
}

ENodeType TListNodeTypeHandler::GetNodeType()
{
    return ENodeType::List;
}

Stroka TListNodeTypeHandler::GetTypeName()
{
    return "list";
}

ICypressNodeProxy::TPtr TListNodeTypeHandler::GetProxy(
    const ICypressNode& node,
    const TTransactionId& transactionId)
{
    return New<TListNodeProxy>(
        this,
        ~CypressManager,
        transactionId,
        node.GetId().NodeId);
}

void TListNodeTypeHandler::DoDestroy(TListNode& node)
{
    // Drop references to the children.
    FOREACH (auto& nodeId, node.IndexToChild()) {
        CypressManager->GetObjectManager()->UnrefObject(nodeId);
    }
}

void TListNodeTypeHandler::DoBranch(
    const TListNode& committedNode,
    TListNode& branchedNode)
{
    UNUSED(branchedNode);

    // Reference all children.
    FOREACH (const auto& nodeId, committedNode.IndexToChild()) {
        CypressManager->GetObjectManager()->RefObject(nodeId);
    }
}

void TListNodeTypeHandler::DoMerge(
    TListNode& committedNode,
    TListNode& branchedNode)
{
    // Drop all references held by the originator.
    FOREACH (const auto& nodeId, committedNode.IndexToChild()) {
        CypressManager->GetObjectManager()->UnrefObject(nodeId);
    }

    // Replace the child list with the branched copy.
    committedNode.IndexToChild().swap(branchedNode.IndexToChild());
    committedNode.ChildToIndex().swap(branchedNode.ChildToIndex());
}

void TListNodeTypeHandler::GetSize(const TGetAttributeParam& param)
{
    BuildYsonFluently(param.Consumer)
        .Scalar(param.Node->IndexToChild().ysize());
}

////////////////////////////////////////////////////////////////////////////////

TRootNodeTypeHandler::TRootNodeTypeHandler(TCypressManager* cypressManager)
    : TMapNodeTypeHandler(cypressManager)
{ }

EObjectType TRootNodeTypeHandler::GetObjectType()
{
    return EObjectType::RootNode;
}

Stroka TRootNodeTypeHandler::GetTypeName()
{
    return "root";
}

ICypressNodeProxy::TPtr TRootNodeTypeHandler::GetProxy(
    const ICypressNode& node,
    const TTransactionId& transactionId)
{
    return New<TRootNodeProxy>(
        this,
        ~CypressManager,
        transactionId,
        node.GetId().NodeId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

