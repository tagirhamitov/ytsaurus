#include "stdafx.h"
#include "cypress_manager.h"
#include "node_detail.h"
#include "node_proxy_detail.h"
#include "config.h"
#include "access_tracker.h"
#include "lock_proxy.h"
#include "private.h"

#include <ytlib/misc/singleton.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/cypress_client/cypress_ypath.pb.h>

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/ytree/ephemeral_node_factory.h>
#include <ytlib/ytree/ypath_detail.h>

#include <server/cell_master/serialization_context.h>
#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>

#include <server/object_server/type_handler_detail.h>

#include <server/security_server/account.h>
#include <server/security_server/group.h>
#include <server/security_server/user.h>
#include <server/security_server/security_manager.h>

namespace NYT {
namespace NCypressServer {

using namespace NCellMaster;
using namespace NBus;
using namespace NRpc;
using namespace NYTree;
using namespace NTransactionServer;
using namespace NMetaState;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NSecurityClient;
using namespace NSecurityServer;
using namespace NCypressClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = CypressServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TNodeTypeHandler
    : public TObjectTypeHandlerBase<TCypressNodeBase>
{
public:
    TNodeTypeHandler(TBootstrap* bootstrap, EObjectType type)
        : TObjectTypeHandlerBase(bootstrap)
        , Type(type)
    { }

    virtual EObjectType GetType() const override
    {
        return Type;
    }

    virtual TObjectBase* FindObject(const TObjectId& id) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        return cypressManager->FindNode(TVersionedNodeId(id));
    }

    virtual void Destroy(TObjectBase* object) override
    {
        DoDestroy(static_cast<TCypressNodeBase*>(object));
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return TTypeCreationOptions(
            EObjectTransactionMode::Optional,
            EObjectAccountMode::Forbidden,
            false);
    }

    virtual EPermissionSet GetSupportedPermissions() const override
    {
        return EPermissionSet(
            EPermission::Read |
            EPermission::Write |
            EPermission::Administer);
    }

private:
    EObjectType Type;

    void DoDestroy(TCypressNodeBase* node)
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        cypressManager->DestroyNode(node);
    }

    virtual Stroka DoGetName(TCypressNodeBase* node) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        auto path = cypressManager->GetNodePath(node->GetTrunkNode(), node->GetTransaction());
        return Sprintf("node %s", ~path);
    }

    virtual IObjectProxyPtr DoGetProxy(
        TCypressNodeBase* node,
        TTransaction* transaction) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        return cypressManager->GetNodeProxy(node, transaction);
    }

    virtual TAccessControlDescriptor* DoFindAcd(TCypressNodeBase* node) override
    {
        return &node->GetTrunkNode()->Acd();
    }

    virtual TObjectBase* DoGetParent(TCypressNodeBase* node) override
    {
        return node->GetParent();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TLockTypeHandler
    : public TObjectTypeHandlerWithMapBase<TLock>
{
public:
    explicit TLockTypeHandler(TCypressManager* owner)
        : TObjectTypeHandlerWithMapBase(owner->Bootstrap, &owner->LockMap)
    { }

    virtual EObjectType GetType() const override
    {
        return EObjectType::Lock;
    }

private:
    virtual Stroka DoGetName(TLock* lock) override
    {
        return Sprintf("node %s", ~ToString(lock->GetId()));
    }

    virtual IObjectProxyPtr DoGetProxy(
        TLock* lock,
        TTransaction* /*transaction*/) override
    {
        return CreateLockProxy(Bootstrap, lock);
    }

};

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TYPathResolver
    : public INodeResolver
{
public:
    TYPathResolver(
        TBootstrap* bootstrap,
        TTransaction* transaction)
        : Bootstrap(bootstrap)
        , Transaction(transaction)
    { }

    virtual INodePtr ResolvePath(const TYPath& path) override
    {
        auto objectManager = Bootstrap->GetObjectManager();
        auto* resolver = objectManager->GetObjectResolver();
        auto objectProxy = resolver->ResolvePath(path, Transaction);
        auto* nodeProxy = dynamic_cast<ICypressNodeProxy*>(~objectProxy);
        if (!nodeProxy) {
            THROW_ERROR_EXCEPTION("Path % points to a nonversioned %s object instead of a node",
                ~FormatEnum(TypeFromId(objectProxy->GetId())).Quote());
        }
        return nodeProxy;
    }

    virtual TYPath GetPath(INodePtr node) override
    {
        INodePtr root;
        auto path = GetNodeYPath(node, &root);

        auto* rootProxy = dynamic_cast<ICypressNodeProxy*>(~root);
        YCHECK(rootProxy);

        auto cypressManager = Bootstrap->GetCypressManager();
        auto rootId = cypressManager->GetRootNode()->GetId();
        return rootProxy->GetId() == rootId
            ? "/" + path
            : "?" + path;
    }

private:
    TBootstrap* Bootstrap;
    TTransaction* Transaction;

};

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TNodeMapTraits::TNodeMapTraits(TCypressManager* cypressManager)
    : CypressManager(cypressManager)
{ }

std::unique_ptr<TCypressNodeBase> TCypressManager::TNodeMapTraits::Create(const TVersionedNodeId& id) const
{
    auto type = TypeFromId(id.ObjectId);
    auto handler = CypressManager->GetHandler(type);
    return handler->Instantiate(id);
}

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TCypressManager(
    TCypressManagerConfigPtr config,
    TBootstrap* bootstrap)
    : TMetaStatePart(
        bootstrap->GetMetaStateFacade()->GetManager(),
        bootstrap->GetMetaStateFacade()->GetState())
    , Config(config)
    , Bootstrap(bootstrap)
    , NodeMap(TNodeMapTraits(this))
    , TypeToHandler(MaxObjectType + 1)
    , RootNode(nullptr)
    , AccessTracker(New<TAccessTracker>(config, bootstrap))
{
    YCHECK(config);
    YCHECK(bootstrap);
    VERIFY_INVOKER_AFFINITY(bootstrap->GetMetaStateFacade()->GetInvoker(), StateThread);

    {
        auto cellId = Bootstrap->GetObjectManager()->GetCellId();
        RootNodeId = MakeWellKnownId(EObjectType::MapNode, cellId);
    }

    RegisterHandler(New<TStringNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TIntegerNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TDoubleNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TMapNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TListNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TLinkNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TDocumentNodeTypeHandler>(Bootstrap));

    {
        NCellMaster::TLoadContext context;
        context.SetBootstrap(Bootstrap);

        RegisterLoader(
            "Cypress.Keys",
            SnapshotVersionValidator(),
            BIND(&TCypressManager::LoadKeys, MakeStrong(this)),
            context);
        RegisterLoader(
            "Cypress.Values",
            SnapshotVersionValidator(),
            BIND(&TCypressManager::LoadValues, MakeStrong(this)),
            context);
    }

    {
        NCellMaster::TSaveContext context;

        RegisterSaver(
            ESerializationPriority::Keys,
            "Cypress.Keys",
            GetCurrentSnapshotVersion(),
            BIND(&TCypressManager::SaveKeys, MakeStrong(this)),
            context);
        RegisterSaver(
            ESerializationPriority::Values,
            "Cypress.Values",
            GetCurrentSnapshotVersion(),
            BIND(&TCypressManager::SaveValues, MakeStrong(this)),
            context);
    }

    RegisterMethod(BIND(&TThis::UpdateAccessStatistics, Unretained(this)));
}

void TCypressManager::Initialize()
{
    auto transactionManager = Bootstrap->GetTransactionManager();
    transactionManager->SubscribeTransactionCommitted(BIND(
        &TThis::OnTransactionCommitted,
        MakeStrong(this)));
    transactionManager->SubscribeTransactionAborted(BIND(
        &TThis::OnTransactionAborted,
        MakeStrong(this)));

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->RegisterHandler(New<TLockTypeHandler>(this));
}

void TCypressManager::RegisterHandler(INodeTypeHandlerPtr handler)
{
    // No thread affinity is given here.
    // This will be called during init-time only.
    YCHECK(handler);

    auto type = handler->GetObjectType();
    int typeValue = static_cast<int>(type);
    YCHECK(typeValue >= 0 && typeValue <= MaxObjectType);
    YCHECK(!TypeToHandler[typeValue]);
    TypeToHandler[typeValue] = handler;

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->RegisterHandler(New<TNodeTypeHandler>(Bootstrap, type));
}

INodeTypeHandlerPtr TCypressManager::FindHandler(EObjectType type)
{
    VERIFY_THREAD_AFFINITY_ANY();

    int typeValue = static_cast<int>(type);
    if (typeValue < 0 || typeValue > MaxObjectType) {
        return nullptr;
    }

    return TypeToHandler[typeValue];
}

INodeTypeHandlerPtr TCypressManager::GetHandler(EObjectType type)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto handler = FindHandler(type);
    YCHECK(handler);
    return handler;
}

INodeTypeHandlerPtr TCypressManager::GetHandler(const TCypressNodeBase* node)
{
    return GetHandler(node->GetType());
}

NMetaState::TMutationPtr TCypressManager::CreateUpdateAccessStatisticsMutation(
    const NProto::TMetaReqUpdateAccessStatistics& request)
{
   return Bootstrap
        ->GetMetaStateFacade()
        ->CreateMutation(this, request, &TThis::UpdateAccessStatistics);
}

TCypressNodeBase* TCypressManager::CreateNode(
    INodeTypeHandlerPtr handler,
    ICypressNodeFactoryPtr factory,
    TReqCreate* request,
    TRspCreate* response)
{
    YCHECK(handler);
    YCHECK(factory);

    auto* transaction = factory->GetTransaction();
    auto node = handler->Create(transaction, request, response);
    auto node_ = ~node;

    RegisterNode(std::move(node));

    // Set account.
    auto securityManager = Bootstrap->GetSecurityManager();
    auto* account = factory->GetAccount();
    securityManager->SetAccount(node_, account);

    // Set owner.
    auto* user = securityManager->GetAuthenticatedUser();
    auto* acd = securityManager->GetAcd(node_);
    acd->SetOwner(user);

    if (response) {
        ToProto(response->mutable_node_id(), node_->GetId());
    }

    return node_;
}

TCypressNodeBase* TCypressManager::CloneNode(
    TCypressNodeBase* sourceNode,
    ICypressNodeFactoryPtr factory)
{
    YCHECK(sourceNode);
    YCHECK(factory);

    auto handler = GetHandler(sourceNode);
    auto clonedNode = handler->Clone(sourceNode, factory);

    // Make a rawptr copy and transfer the ownership.
    auto clonedNode_ = ~clonedNode;
    RegisterNode(std::move(clonedNode));

    // Set account.
    auto securityManager = Bootstrap->GetSecurityManager();
    auto* account = factory->GetAccount();
    securityManager->SetAccount(clonedNode_, account);

    // Set owner.
    auto* user = securityManager->GetAuthenticatedUser();
    auto* acd = securityManager->GetAcd(clonedNode_);
    acd->SetOwner(user);

    return clonedNode_;
}

TCypressNodeBase* TCypressManager::GetRootNode() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return RootNode;
}

INodeResolverPtr TCypressManager::CreateResolver(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    return New<TYPathResolver>(Bootstrap, transaction);
}

TCypressNodeBase* TCypressManager::FindNode(
    TCypressNodeBase* trunkNode,
    NTransactionServer::TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    // Fast path -- no transaction.
    if (!transaction) {
        return trunkNode;
    }

    TVersionedNodeId versionedId(trunkNode->GetId(), GetObjectId(transaction));
    return FindNode(versionedId);
}

TCypressNodeBase* TCypressManager::GetVersionedNode(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto* currentTransaction = transaction;
    while (true) {
        auto* currentNode = FindNode(trunkNode, currentTransaction);
        if (currentNode) {
            return currentNode;
        }
        currentTransaction = currentTransaction->GetParent();
    }
}

ICypressNodeProxyPtr TCypressManager::GetNodeProxy(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto handler = GetHandler(trunkNode);
    return handler->GetProxy(trunkNode, transaction);
}

TError TCypressManager::ValidateLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request,
    bool checkPending,
    bool* isMandatory)
{
    YCHECK(trunkNode->IsTrunk());

    *isMandatory = true;

    // Snapshot locks can only be taken inside a transaction.
    if (request.Mode == ELockMode::Snapshot && !transaction) {
        return TError("%s lock requires a transaction",
            ~FormatEnum(request.Mode).Quote());
    }

    // Examine existing locks.
    // A quick check: same transaction, same or weaker lock mode (beware of Snapshot!).
    {
        auto it = trunkNode->LockStateMap().find(transaction);
        if (it != trunkNode->LockStateMap().end()) {
            const auto& existingState = it->second;
            if (IsRedundantLockRequest(existingState, request)) {
                *isMandatory = false;
                return TError();
            }
            if (existingState.Mode == ELockMode::Snapshot) {
                return TError(
                    NCypressClient::EErrorCode::SameTransactionLockConflict,
                    "Cannot take %s lock for node %s since %s lock is already taken by the same transaction",
                    ~FormatEnum(request.Mode).Quote(),
                    ~GetNodePath(trunkNode, transaction),
                    ~FormatEnum(existingState.Mode).Quote());
            }
        }
    }

    FOREACH (const auto& pair, trunkNode->LockStateMap()) {
        auto* existingTransaction = pair.first;
        const auto& existingState = pair.second;

        // Ignore other Snapshot locks.
        if (existingState.Mode == ELockMode::Snapshot) {
            continue;
        }

        // When a Snapshot is requested no descendant transaction (including |transaction| itself)
        // may hold a lock other than Snapshot.
        if (request.Mode == ELockMode::Snapshot &&
            IsParentTransaction(existingTransaction, transaction))
        {
            return TError(
                NCypressClient::EErrorCode::DescendantTransactionLockConflict,
                "Cannot take %s lock for node %s since %s lock is taken by descendant transaction %s",
                ~FormatEnum(request.Mode).Quote(),
                ~GetNodePath(trunkNode, transaction),
                ~FormatEnum(existingState.Mode).Quote(),
                ~ToString(existingTransaction->GetId()));
        }

        if (!transaction || IsConcurrentTransaction(transaction, existingTransaction)) {
            // For Exclusive locks we check locks held by concurrent transactions.
            if ((request.Mode == ELockMode::Exclusive && existingState.Mode != ELockMode::Snapshot) ||
                (existingState.Mode == ELockMode::Exclusive && request.Mode != ELockMode::Snapshot))
            {
                return TError(
                    NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                    "Cannot take %s lock for node %s since %s lock is taken by concurrent transaction %s",
                    ~FormatEnum(request.Mode).Quote(),
                    ~GetNodePath(trunkNode, transaction),
                    ~FormatEnum(existingState.Mode).Quote(),
                    ~ToString(existingTransaction->GetId()));
            }

            // For Shared locks we check child and attribute keys.
            if (request.Mode == ELockMode::Shared && existingState.Mode == ELockMode::Shared) {
                if (request.ChildKey &&
                    existingState.ChildKeys.find(request.ChildKey.Get()) != existingState.ChildKeys.end())
                {
                    return TError(
                        NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                        "Cannot take %s lock for child %s of node %s since %s lock is taken by concurrent transaction %s",
                        ~FormatEnum(request.Mode).Quote(),
                        ~request.ChildKey.Get().Quote(),
                        ~GetNodePath(trunkNode, transaction),
                        ~FormatEnum(existingState.Mode).Quote(),
                        ~ToString(existingTransaction->GetId()));
                }
                if (request.AttributeKey &&
                    existingState.AttributeKeys.find(request.AttributeKey.Get()) != existingState.AttributeKeys.end())
                {
                    return TError(
                        NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                        "Cannot take %s lock for attribute %s of node %s since %s lock is taken by concurrent transaction %s",
                        ~FormatEnum(request.Mode).Quote(),
                        ~request.AttributeKey.Get().Quote(),
                        ~GetNodePath(trunkNode, transaction),
                        ~FormatEnum(existingState.Mode).Quote(),
                        ~ToString(existingTransaction->GetId()));
                }
            }
        }
    }

    // If we're outside of a transaction then the lock is not needed.
    if (!transaction) {
        *isMandatory = false;
    }   

    // Check pending locks.
    if (checkPending &&
        !trunkNode->LockList().empty() &&
        trunkNode->LockList().back()->GetState() == ELockState::Pending)
    {
        return TError(
            NCypressClient::EErrorCode::PendingLockConflict,
            "Cannot take %s lock for node %s since there are pending locks for this node",
            ~FormatEnum(request.Mode).Quote(),
            ~GetNodePath(trunkNode, transaction));
    }

    return TError();
}

bool TCypressManager::IsRedundantLockRequest(
    const TTransactionLockState& state,
    const TLockRequest& request)
{
    if (state.Mode > request.Mode && request.Mode != ELockMode::Snapshot) {
        return true;
    }

    if (state.Mode == request.Mode) {
        if (request.Mode == ELockMode::Shared) {
            if (request.ChildKey &&
                state.ChildKeys.find(request.ChildKey.Get()) == state.ChildKeys.end())
            {
                return false;
            }
            if (request.AttributeKey &&
                state.AttributeKeys.find(request.AttributeKey.Get()) == state.AttributeKeys.end())
            {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool TCypressManager::IsParentTransaction(
    TTransaction* transaction,
    TTransaction* parent)
{
    auto currentTransaction = transaction;
    while (currentTransaction) {
        if (currentTransaction == parent) {
            return true;
        }
        currentTransaction = currentTransaction->GetParent();
    }
    return false;
}

bool TCypressManager::IsConcurrentTransaction(
    TTransaction* transaction1,
    TTransaction* transaction2)
{
    return
        !IsParentTransaction(transaction1, transaction2) &&
        !IsParentTransaction(transaction2, transaction1);
}

TCypressNodeBase* TCypressManager::DoLockNode(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    YCHECK(trunkNode->IsTrunk());
    YCHECK(transaction);

    UpdateNodeLockState(trunkNode, transaction, request);

    // Upgrade locks held by parent transactions, if needed.
    if (request.Mode != ELockMode::Snapshot) {
        auto* currentTransaction = transaction->GetParent();
        while (currentTransaction) {
            UpdateNodeLockState(trunkNode, currentTransaction, request);
            currentTransaction = currentTransaction->GetParent();
        }
    }

    // Branch node, if needed.
    auto* branchedNode = FindNode(trunkNode, transaction);
    if (branchedNode) {
        if (branchedNode->GetLockMode() < request.Mode) {
            branchedNode->SetLockMode(request.Mode);
        }
        return branchedNode;
    }

    TCypressNodeBase* originatingNode;
    std::vector<TTransaction*> intermediateTransactions;
    // Walk up to the root, find originatingNode, construct the list of
    // intermediate transactions.
    auto* currentTransaction = transaction;
    while (true) {
        originatingNode = FindNode(trunkNode, currentTransaction);
        if (originatingNode) {
            break;
        }
        if (!currentTransaction) {
            break;
        }
        intermediateTransactions.push_back(currentTransaction);
        currentTransaction = currentTransaction->GetParent();
    }

    YCHECK(originatingNode);
    YCHECK(!intermediateTransactions.empty());

    if (request.Mode == ELockMode::Snapshot) {
        // Branch at requested transaction only.
        return BranchNode(originatingNode, transaction, request.Mode);
    } else {
        // Branch at all intermediate transactions.
        std::reverse(intermediateTransactions.begin(), intermediateTransactions.end());
        auto* currentNode = originatingNode;
        FOREACH (auto* transactionToBranch, intermediateTransactions) {
            currentNode = BranchNode(currentNode, transactionToBranch, request.Mode);
        }
        return currentNode;
    }
}

void TCypressManager::UpdateNodeLockState(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    YCHECK(trunkNode->IsTrunk());

    TVersionedNodeId versionedId(trunkNode->GetId(), transaction->GetId());
    TTransactionLockState* lockState;
    auto it = trunkNode->LockStateMap().find(transaction);
    if (it == trunkNode->LockStateMap().end()) {
        lockState = &trunkNode->LockStateMap()[transaction];
        lockState->Mode = request.Mode;
        YCHECK(transaction->LockedNodes().insert(trunkNode).second);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node locked (NodeId: %s, Mode: %s)",
            ~ToString(versionedId),
            ~request.Mode.ToString());
    } else {
        lockState = &it->second;
        if (lockState->Mode < request.Mode) {
            lockState->Mode = request.Mode;

            LOG_DEBUG_UNLESS(IsRecovery(), "Node lock upgraded (NodeId: %s, Mode: %s)",
                ~ToString(versionedId),
                ~lockState->Mode.ToString());
        }
    }

    if (request.ChildKey &&
        lockState->ChildKeys.find(request.ChildKey.Get()) == lockState->ChildKeys.end())
    {
        YCHECK(lockState->ChildKeys.insert(request.ChildKey.Get()).second);
        LOG_DEBUG_UNLESS(IsRecovery(), "Node child locked (NodeId: %s, Key: %s)",
            ~ToString(versionedId),
            ~request.ChildKey.Get());
    }

    if (request.AttributeKey &&
        lockState->AttributeKeys.find(request.AttributeKey.Get()) == lockState->AttributeKeys.end())
    {
        YCHECK(lockState->AttributeKeys.insert(request.AttributeKey.Get()).second);
        LOG_DEBUG_UNLESS(IsRecovery(), "Node attribute locked (NodeId: %s, Key: %s)",
            ~ToString(versionedId),
            ~request.AttributeKey.Get());
    }
}

TLock* TCypressManager::DoCreateLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    auto objectManager = Bootstrap->GetObjectManager();

    auto id = objectManager->GenerateId(EObjectType::Lock);

    auto* lock  = new TLock(id);
    lock->SetState(ELockState::Pending);
    lock->SetTrunkNode(trunkNode);
    lock->SetTransaction(transaction);
    lock->Request() = request;
    trunkNode->LockList().push_back(lock);
    lock->SetLockListIterator(--trunkNode->LockList().end());
    LockMap.Insert(id, lock);

    YCHECK(transaction->Locks().insert(lock).second);
    objectManager->RefObject(lock);
     
    LOG_DEBUG_UNLESS(IsRecovery(), "Lock created (LockId: %s, Mode: %s, NodeId: %s)",
        ~ToString(id),
        ~request.Mode.ToString(),
        ~ToString(TVersionedNodeId(trunkNode->GetId(), transaction->GetId())));

    return lock;
}

void TCypressManager::SetLockAcquired(TLock* lock)
{
    LOG_DEBUG_UNLESS(IsRecovery(), "Lock acquired (LockId: %s)",
        ~ToString(lock->GetId()));
    lock->SetState(ELockState::Acquired);
}

TCypressNodeBase* TCypressManager::LockNode(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request,
    bool recursive)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());
    YCHECK(request.Mode != ELockMode::None);

    TSubtreeNodes childrenToLock;
    if (recursive) {
        YCHECK(!request.ChildKey);
        YCHECK(!request.AttributeKey);
        ListSubtreeNodes(trunkNode, transaction, true, &childrenToLock);
    } else {
        childrenToLock.push_back(trunkNode);
    }

    // Validate all potentials lock to see if we need to take at least one of them.
    // This throws an exception in case the validation fails.
    bool isMandatory = false;
    FOREACH (auto* child, childrenToLock) {
        auto* trunkChild = child->GetTrunkNode();

        bool isChildMandatory;
        auto error = ValidateLock(
            trunkChild,
            transaction,
            request,
            true,
            &isChildMandatory);
        
        if (!error.IsOK()) {
            THROW_ERROR error;
        }
        
        isMandatory |= isChildMandatory;
    }

    if (!isMandatory) {
        return GetVersionedNode(trunkNode, transaction);
    }

    // Ensure deterministic order of children.
    std::sort(
        childrenToLock.begin(),
        childrenToLock.end(),
        [] (const TCypressNodeBase* lhs, const TCypressNodeBase* rhs) {
            return CompareObjectsForSerialization(lhs, rhs);
        });

    TCypressNodeBase* lockedNode = nullptr;
    FOREACH (auto* child, childrenToLock) {
        auto* lock = DoCreateLock(child, transaction, request);
        SetLockAcquired(lock);

        auto* lockedChild = DoLockNode(child->GetTrunkNode(), transaction, request);
        if (child == trunkNode) {
            lockedNode = lockedChild;
        }
    }

    YCHECK(lockedNode);
    return lockedNode;
}

TLock* TCypressManager::CreateLock(
    TCypressNodeBase* trunkNode,
    NTransactionServer::TTransaction* transaction,
    const TLockRequest& request,
    bool waitable)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());
    YCHECK(transaction);
    YCHECK(request.Mode != ELockMode::None);

    if (waitable && !transaction) {
        THROW_ERROR_EXCEPTION("Waitable lock requires a transaction");
    }

    // Try to lock without waiting in the queue.
    bool isMandatory;
    auto error = ValidateLock(
        trunkNode,
        transaction,
        request,
        true,
        &isMandatory);

    // Is it OK?
    if (error.IsOK()) {
        if (!isMandatory) {
            return nullptr;
        }
        
        auto* lock = DoCreateLock(trunkNode, transaction, request);
        lock->SetState(ELockState::Acquired);

        DoLockNode(trunkNode, transaction, request);
        return lock;
    }

    // Should we wait?
    if (!waitable) {
        THROW_ERROR error;
    }

    // Will wait.
    YCHECK(isMandatory);
    return DoCreateLock(trunkNode, transaction, request);
}

void TCypressManager::CheckPendingLocks(TCypressNodeBase* trunkNode)
{
    // Ignore orphaned nodes.
    // Eventually the node will get destroyed and the lock will become
    // orphaned.
    if (IsOrphaned(trunkNode))
        return;

    // Find the first pending lock.
    // Expect few pending locks but possibly a huge number of active ones.
    // Hence just scan backwards from the end.
    auto it = trunkNode->LockList().end();
    auto jt = it;
    while (it != trunkNode->LockList().begin()) {
        --it;
        auto* lock = *it;
        if (lock->GetState() == ELockState::Acquired)
            break;
        jt = it;
    }

    // Scan forward and make acquisitions while possible.
    while (jt != trunkNode->LockList().end()) {
        auto* lock = *jt;

        bool isMandatory;
        auto error = ValidateLock(
            trunkNode,
            lock->GetTransaction(),
            lock->Request(),
            false,
            &isMandatory);

        // Is it OK?
        if (!error.IsOK())
            return;

        if (isMandatory) {
            UpdateNodeLockState(trunkNode, lock->GetTransaction(), lock->Request());
        }

        SetLockAcquired(lock);

        ++jt;
    }
}

void TCypressManager::SetModified(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->SetModified(trunkNode, transaction);
}

void TCypressManager::SetAccessed(TCypressNodeBase* trunkNode)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    if (IsLeader()) {
        AccessTracker->SetAccessed(trunkNode);
    }
}

TCypressManager::TSubtreeNodes TCypressManager::ListSubtreeNodes(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    bool includeRoot)
{
    TSubtreeNodes result;
    ListSubtreeNodes(trunkNode, transaction, includeRoot, &result);
    return result;
}

bool TCypressManager::IsOrphaned(TCypressNodeBase* trunkNode)
{
    while (true) {
        if (!IsObjectAlive(trunkNode)) {
            return true;
        }
        if (trunkNode == RootNode) {
            return false;
        }
        trunkNode = trunkNode->GetParent();
    }
}

TCypressNodeBase* TCypressManager::BranchNode(
    TCypressNodeBase* originatingNode,
    TTransaction* transaction,
    ELockMode mode)
{
    YCHECK(originatingNode);
    YCHECK(transaction);
    VERIFY_THREAD_AFFINITY(StateThread);

    auto objectManager = Bootstrap->GetObjectManager();
    auto securityManager = Bootstrap->GetSecurityManager();

    const auto& id = originatingNode->GetId();

    // Create a branched node and initialize its state.
    auto handler = GetHandler(originatingNode);
    auto branchedNode = handler->Branch(originatingNode, transaction, mode);
    YCHECK(branchedNode->GetLockMode() == mode);
    auto* branchedNode_ = branchedNode.release();

    TVersionedNodeId versionedId(id, transaction->GetId());
    NodeMap.Insert(versionedId, branchedNode_);

    // Register the branched node with the transaction.
    transaction->BranchedNodes().push_back(branchedNode_);

    // The branched node holds an implicit reference to its originator.
    objectManager->RefObject(originatingNode->GetTrunkNode());

    // Update resource usage.
    auto* account = originatingNode->GetAccount();
    securityManager->SetAccount(branchedNode_, account);

    LOG_DEBUG_UNLESS(IsRecovery(), "Node branched (NodeId: %s, Mode: %s)",
        ~ToString(TVersionedNodeId(id, transaction->GetId())),
        ~mode.ToString());

    return branchedNode_;
}

void TCypressManager::SaveKeys(NCellMaster::TSaveContext& context) const
{
    NodeMap.SaveKeys(context);
    LockMap.SaveKeys(context);
}

void TCypressManager::SaveValues(NCellMaster::TSaveContext& context) const
{
    NodeMap.SaveValues(context);
    LockMap.SaveValues(context);
}

void TCypressManager::OnBeforeLoaded()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    DoClear();
}

void TCypressManager::LoadKeys(NCellMaster::TLoadContext& context)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadKeys(context);
    // COMPAT(babenko)
    if (context.GetVersion() >= 24) {
        LockMap.LoadKeys(context);
    }
}

void TCypressManager::LoadValues(NCellMaster::TLoadContext& context)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadValues(context);
    // COMPAT(babenko)
    if (context.GetVersion() >= 24) {
        LockMap.LoadValues(context);
    }
}

void TCypressManager::OnAfterLoaded()
{
    // Reconstruct immediate ancestor sets.
    FOREACH (const auto& pair, NodeMap) {
        auto* node = pair.second;
        auto* parent = node->GetParent();
        if (parent) {
            YCHECK(parent->ImmediateDescendants().insert(node).second);
        }
    }

    InitBuiltin();
}

void TCypressManager::InitBuiltin()
{
    RootNode = FindNode(TVersionedNodeId(RootNodeId));
    if (!RootNode) {
        // Create the root.
        auto securityManager = Bootstrap->GetSecurityManager();
        RootNode = new TMapNode(TVersionedNodeId(RootNodeId));
        RootNode->SetTrunkNode(RootNode);
        RootNode->SetAccount(securityManager->GetSysAccount());
        RootNode->Acd().SetInherit(false);
        RootNode->Acd().AddEntry(TAccessControlEntry(
            ESecurityAction::Allow,
            securityManager->GetEveryoneGroup(),
            EPermission::Read));
        RootNode->Acd().SetOwner(securityManager->GetRootUser());

        NodeMap.Insert(TVersionedNodeId(RootNodeId), RootNode);
        YCHECK(RootNode->RefObject() == 1);
    }
}

void TCypressManager::DoClear()
{
    NodeMap.Clear();
    LockMap.Clear();
}

void TCypressManager::Clear()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    DoClear();
    InitBuiltin();
}

void TCypressManager::OnRecoveryComplete()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    FOREACH (const auto& pair, NodeMap) {
        auto* node = pair.second;
        node->ResetWeakRefCounter();
    }
}

void TCypressManager::RegisterNode(std::unique_ptr<TCypressNodeBase> node)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(node->IsTrunk());

    const auto& nodeId = node->GetId();

    auto objectManager = Bootstrap->GetObjectManager();

    auto* mutationContext = Bootstrap
        ->GetMetaStateFacade()
        ->GetManager()
        ->GetMutationContext();

    node->SetCreationTime(mutationContext->GetTimestamp());
    node->SetModificationTime(mutationContext->GetTimestamp());
    node->SetRevision(mutationContext->GetVersion().ToRevision());

    NodeMap.Insert(TVersionedNodeId(nodeId), node.release());

    LOG_DEBUG_UNLESS(IsRecovery(), "Node registered (NodeId: %s, Type: %s)",
        ~ToString(nodeId),
        ~TypeFromId(nodeId).ToString());
}

void TCypressManager::DestroyNode(TCypressNodeBase* trunkNode)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto nodeHolder = NodeMap.Release(trunkNode->GetVersionedId());

    TCypressNodeBase::TLockList lockList;
    trunkNode->LockList().swap(lockList);

    TCypressNodeBase::TLockStateMap lockStateMap;
    trunkNode->LockStateMap().swap(lockStateMap);

    auto objectManager = Bootstrap->GetObjectManager();

    FOREACH (auto* lock, lockList) {
        if (lock->GetState() == ELockState::Pending) {
            LOG_DEBUG_UNLESS(IsRecovery(), "Lock orphaned (LockId: %s)",
                ~ToString(lock->GetId()));
            auto* transaction = lock->GetTransaction();
            YCHECK(transaction->Locks().erase(lock) == 1);
            objectManager->UnrefObject(lock);
        }
        lock->SetTrunkNode(nullptr);
    }

    FOREACH (const auto& pair, lockStateMap) {
        auto* transaction = pair.first;
        YCHECK(transaction->LockedNodes().erase(trunkNode) == 1);
    }

    auto handler = GetHandler(trunkNode);
    handler->Destroy(trunkNode);
}

void TCypressManager::OnTransactionCommitted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    MergeNodes(transaction);
    ReleaseLocks(transaction);
}

void TCypressManager::OnTransactionAborted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    RemoveBranchedNodes(transaction);
    ReleaseLocks(transaction);
}

void TCypressManager::ReleaseLocks(TTransaction* transaction)
{
    auto objectManager = Bootstrap->GetObjectManager();

    TTransaction::TLockSet locks;
    transaction->Locks().swap(locks);

    TTransaction::TLockedNodeSet lockedNodes;
    transaction->LockedNodes().swap(lockedNodes);

    FOREACH (auto* lock, locks) {
        auto* trunkNode = lock->GetTrunkNode();
        if (trunkNode) {
            trunkNode->LockList().erase(lock->GetLockListIterator());
            lock->SetTrunkNode(nullptr);
        }
        objectManager->UnrefObject(lock);
    }

    FOREACH (auto* trunkNode, lockedNodes) {
        YCHECK(trunkNode->LockStateMap().erase(transaction) == 1);

        TVersionedNodeId versionedId(trunkNode->GetId(), transaction->GetId());
        LOG_DEBUG_UNLESS(IsRecovery(), "Node unlocked (NodeId: %s)",
            ~ToString(versionedId));
    }

    FOREACH (auto* trunkNode, lockedNodes) {
        CheckPendingLocks(trunkNode);
    }
}

void TCypressManager::ListSubtreeNodes(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    bool includeRoot,
    TSubtreeNodes* subtreeNodes)
{
    YCHECK(trunkNode->IsTrunk());

    auto transactionManager = Bootstrap->GetTransactionManager();

    if (includeRoot) {
        subtreeNodes->push_back(trunkNode);
    }

    switch (trunkNode->GetType()) {
        case EObjectType::MapNode: {
            auto transactions = transactionManager->GetTransactionPath(transaction);
            std::reverse(transactions.begin(), transactions.end());

            yhash_map<Stroka, TCypressNodeBase*> children;
            FOREACH (auto* currentTransaction, transactions) {
                TVersionedObjectId versionedId(trunkNode->GetId(), GetObjectId(currentTransaction));
                const auto* node = FindNode(versionedId);
                if (node) {
                    const auto* mapNode = static_cast<const TMapNode*>(node);
                    FOREACH (const auto& pair, mapNode->KeyToChild()) {
                        if (pair.second) {
                            children[pair.first] = pair.second;
                        } else {
                            // NB: erase may fail.
                            children.erase(pair.first);
                        }
                    }
                }
            }

            FOREACH (const auto& pair, children) {
                ListSubtreeNodes(pair.second, transaction, true, subtreeNodes);
            }

            break;
        }

        case EObjectType::ListNode: {
            auto* node = GetVersionedNode(trunkNode, transaction);
            auto* listRoot = static_cast<TListNode*>(node);
            FOREACH (auto* trunkChild, listRoot->IndexToChild()) {
                ListSubtreeNodes(trunkChild, transaction, true, subtreeNodes);
            }
            break;
        }

        default:
            break;
    }
}

void TCypressManager::MergeNode(
    TTransaction* transaction,
    TCypressNodeBase* branchedNode)
{
    auto objectManager = Bootstrap->GetObjectManager();
    auto securityManager = Bootstrap->GetSecurityManager();

    auto handler = GetHandler(branchedNode);

    auto* trunkNode = branchedNode->GetTrunkNode();
    auto branchedId = branchedNode->GetVersionedId();
    auto* parentTransaction = transaction->GetParent();
    auto originatingId = TVersionedNodeId(branchedId.ObjectId, GetObjectId(parentTransaction));

    if (branchedNode->GetLockMode() != ELockMode::Snapshot) {
        auto* originatingNode = NodeMap.Get(originatingId);

        // Merge changes back.
        handler->Merge(originatingNode, branchedNode);

        // The root needs a special handling.
        // When Cypress gets cleared, the root is created and is assigned zero creation time.
        // (We don't have any mutation context at hand to provide a synchronized timestamp.)
        // Later on, Cypress is initialized and filled with nodes.
        // At this point we set the root's creation time.
        if (trunkNode == RootNode && !parentTransaction) {
            originatingNode->SetCreationTime(originatingNode->GetModificationTime());
        }

        // Update resource usage.
        securityManager->UpdateAccountNodeUsage(originatingNode);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node merged (NodeId: %s)", ~ToString(branchedId));
    } else {
        // Destroy the branched copy.
        handler->Destroy(branchedNode);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node snapshot destroyed (NodeId: %s)", ~ToString(branchedId));
    }

    // Drop the implicit reference to the originator.
    objectManager->UnrefObject(trunkNode);

    // Remove the branched copy.
    NodeMap.Remove(branchedId);

    LOG_DEBUG_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~ToString(branchedId));
}

void TCypressManager::MergeNodes(TTransaction* transaction)
{
    FOREACH (auto* node, transaction->BranchedNodes()) {
        MergeNode(transaction, node);
    }
    transaction->BranchedNodes().clear();
}

void TCypressManager::RemoveBranchedNode(TCypressNodeBase* branchedNode)
{
    auto objectManager = Bootstrap->GetObjectManager();

    auto handler = GetHandler(branchedNode);

    auto* trunkNode = branchedNode->GetTrunkNode();
    auto branchedNodeId = branchedNode->GetVersionedId();

    // Drop the implicit reference to the originator.
    objectManager->UnrefObject(trunkNode);

    // Remove the node.
    handler->Destroy(branchedNode);
    NodeMap.Remove(branchedNodeId);

    LOG_DEBUG_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~ToString(branchedNodeId));
}

void TCypressManager::RemoveBranchedNodes(TTransaction* transaction)
{
    FOREACH (auto* branchedNode, transaction->BranchedNodes()) {
        RemoveBranchedNode(branchedNode);
    }
    transaction->BranchedNodes().clear();
}

TYPath TCypressManager::GetNodePath(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    YCHECK(trunkNode->IsTrunk());

    auto proxy = GetNodeProxy(trunkNode, transaction);
    return proxy->GetResolver()->GetPath(proxy);
}

void TCypressManager::OnActiveQuorumEstablished()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->StartFlush();
}

void TCypressManager::OnStopLeading()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->StopFlush();
}

void TCypressManager::UpdateAccessStatistics(const NProto::TMetaReqUpdateAccessStatistics& request)
{
    FOREACH (const auto& update, request.updates()) {
        auto nodeId = FromProto<TNodeId>(update.node_id());
        auto* node = FindNode(TVersionedNodeId(nodeId));
        if (node) {
            // Update access time.
            auto accessTime = TInstant(update.access_time());
            if (accessTime > node->GetAccessTime()) {
                node->SetAccessTime(accessTime);
            }

            // Update access counter.
            i64 accessCounter = node->GetAccessCounter() + update.access_counter_delta();
            node->SetAccessCounter(accessCounter);
        }
    }
}

DEFINE_METAMAP_ACCESSORS(TCypressManager, Node, TCypressNodeBase, TVersionedNodeId, NodeMap);
DEFINE_METAMAP_ACCESSORS(TCypressManager, Lock, TLock, TLockId, LockMap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT
