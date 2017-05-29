#include "api_service.h"
#include "public.h"
#include "private.h"

#include <yt/server/cell_proxy/bootstrap.h>

#include <yt/server/blackbox/cookie_authenticator.h>
#include <yt/server/blackbox/token_authenticator.h>

#include <yt/ytlib/api/native_client.h>
#include <yt/ytlib/api/native_connection.h>
#include <yt/ytlib/api/transaction.h>
#include <yt/ytlib/api/rowset.h>

#include <yt/ytlib/rpc_proxy/api_service_proxy.h>
#include <yt/ytlib/rpc_proxy/helpers.h>

#include <yt/ytlib/security_client/public.h>

#include <yt/ytlib/table_client/name_table.h>
#include <yt/ytlib/table_client/row_buffer.h>

#include <yt/ytlib/tablet_client/wire_protocol.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/misc/serialize.h>

#include <yt/core/rpc/service_detail.h>

namespace NYT {
namespace NRpcProxy {

using namespace NApi;
using namespace NYTree;
using namespace NConcurrency;
using namespace NRpc;
using namespace NCompression;
using namespace NBlackbox;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NObjectClient;
using namespace NTransactionClient;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

struct TApiServiceBufferTag
{ };

class TApiService
    : public TServiceBase
{
public:
    TApiService(
        NCellProxy::TBootstrap* bootstrap)
        : TServiceBase(
            bootstrap->GetControlInvoker(), // TODO(sandello): Better threading here.
            TApiServiceProxy::GetDescriptor(),
            RpcProxyLogger)
        , Bootstrap_(bootstrap)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(StartTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(PingTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CommitTransaction));

        RegisterMethod(RPC_SERVICE_METHOD_DESC(ExistsNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ListNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CreateNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(RemoveNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(SetNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(LockNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CopyNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(MoveNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(LinkNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ConcatenateNodes));

        RegisterMethod(RPC_SERVICE_METHOD_DESC(MountTable));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(UnmountTable));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(RemountTable));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(FreezeTable));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(UnfreezeTable));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ReshardTable));

        RegisterMethod(RPC_SERVICE_METHOD_DESC(LookupRows));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(VersionedLookupRows));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(SelectRows));

        RegisterMethod(RPC_SERVICE_METHOD_DESC(ModifyRows));
    }

private:
    NCellProxy::TBootstrap* const Bootstrap_;

    TSpinLock SpinLock_;
    // TODO(sandello): Introduce expiration times for clients.
    yhash<Stroka, INativeClientPtr> AuthenticatedClients_;

    INativeClientPtr GetAuthenticatedClientOrAbortContext(
        const IServiceContextPtr& context,
        const google::protobuf::Message* request)
    {
        auto replyWithMissingCredentials = [&] () {
            context->Reply(TError(
                NSecurityClient::EErrorCode::AuthenticationError,
                "Request is missing credentials"));
        };

        auto replyWithMissingUserIP = [&] () {
            context->Reply(TError(
                NSecurityClient::EErrorCode::AuthenticationError,
                "Request is missing originating address in credentials"));
        };

        const auto& header = context->GetRequestHeader();
        if (!header.HasExtension(NProto::TCredentialsExt::credentials_ext)) {
            replyWithMissingCredentials();
            return nullptr;
        }

        // TODO(sandello): Use a cache here.
        TAuthenticationResult authenticationResult;
        const auto& credentials = header.GetExtension(NProto::TCredentialsExt::credentials_ext);
        if (!credentials.has_userip()) {
            replyWithMissingUserIP();
            return nullptr;
        }
        if (credentials.has_sessionid() || credentials.has_sslsessionid()) {
            auto asyncAuthenticationResult = Bootstrap_->GetCookieAuthenticator()->Authenticate(
                credentials.sessionid(),
                credentials.sslsessionid(),
                credentials.domain(),
                credentials.userip());
            authenticationResult = WaitFor(asyncAuthenticationResult)
                .ValueOrThrow();
        } else if (credentials.has_token()) {
            auto asyncAuthenticationResult = Bootstrap_->GetTokenAuthenticator()->Authenticate(
                TTokenCredentials{credentials.token(), credentials.userip()});
            authenticationResult = WaitFor(asyncAuthenticationResult)
                .ValueOrThrow();
        } else {
            replyWithMissingCredentials();
            return nullptr;
        }

        const auto& user = context->GetUser();
        if (user != authenticationResult.Login) {
            context->Reply(TError(
                NSecurityClient::EErrorCode::AuthenticationError,
                "Invalid credentials"));
            return nullptr;
        }

        context->SetRequestInfo("Request: %v", request->ShortDebugString());

        {
            auto guard = Guard(SpinLock_);
            auto it = AuthenticatedClients_.find(user);
            auto jt = AuthenticatedClients_.end();
            if (it == jt) {
                const auto& connection = Bootstrap_->GetNativeConnection();
                auto client = connection->CreateNativeClient(TClientOptions(user));
                bool inserted = false;
                std::tie(it, inserted) = AuthenticatedClients_.insert(std::make_pair(user, client));
                YCHECK(inserted);
            }
            return it->second;
        }
    }

    ITransactionPtr GetTransactionOrAbortContext(
        const IServiceContextPtr& context,
        const google::protobuf::Message* request,
        const TTransactionId& transactionId,
        const TTransactionAttachOptions& options)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return nullptr;
        }

        auto transaction = client->AttachTransaction(transactionId, options);

        if (!transaction) {
            context->Reply(TError("No such transaction %v", transactionId));
            return nullptr;
        }

        return transaction;
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, StartTransaction)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        TTransactionStartOptions options;
        if (request->has_timeout()) {
            options.Timeout = NYT::FromProto<TDuration>(request->timeout());
        }
        if (request->has_id()) {
            NYT::FromProto(&options.Id, request->id());
        }
        if (request->has_parent_id()) {
            NYT::FromProto(&options.ParentId, request->parent_id());
        }
        options.AutoAbort = request->auto_abort();
        options.Sticky = request->sticky();
        options.Ping = request->ping();
        options.PingAncestors = request->ping_ancestors();

        client->StartTransaction(NTransactionClient::ETransactionType(request->type()), options)
            .Subscribe(BIND([=] (const TErrorOr<ITransactionPtr>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    const auto& value = result.Value();
                    ToProto(response->mutable_id(), value->GetId());
                    response->set_start_timestamp(value->GetStartTimestamp());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, PingTransaction)
    {
        auto transactionAttachOptions = TTransactionAttachOptions{};
        transactionAttachOptions.Ping = true;
        transactionAttachOptions.PingAncestors = true;
        transactionAttachOptions.Sticky = request->sticky();
        auto transaction = GetTransactionOrAbortContext(
            context,
            request,
            NYT::FromProto<TTransactionId>(request->transaction_id()),
            transactionAttachOptions);
        if (!transaction) {
            return;
        }

        // TODO(sandello): Options!
        transaction->Ping().Subscribe(BIND([=] (const TErrorOr<void>& result) {
            if (!result.IsOK()) {
                context->Reply(result);
            } else {
                context->Reply();
            }
        }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, CommitTransaction)
    {
        auto transactionAttachOptions = TTransactionAttachOptions{};
        transactionAttachOptions.Ping = false;
        transactionAttachOptions.PingAncestors = false;
        transactionAttachOptions.Sticky = request->sticky();
        auto transaction = GetTransactionOrAbortContext(
            context,
            request,
            NYT::FromProto<TTransactionId>(request->transaction_id()),
            transactionAttachOptions);
        if (!transaction) {
            return;
        }

        // TODO(sandello): Options!
        transaction->Commit().Subscribe(BIND([=] (const TErrorOr<TTransactionCommitResult>& result) {
            if (!result.IsOK()) {
                context->Reply(result);
            } else {
                // TODO(sandello): Fill me.
                context->Reply();
            }
        }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, AbortTransaction)
    {
        auto transactionAttachOptions = TTransactionAttachOptions{};
        transactionAttachOptions.Ping = false;
        transactionAttachOptions.PingAncestors = false;
        transactionAttachOptions.Sticky = request->sticky();
        auto transaction = GetTransactionOrAbortContext(
            context,
            request,
            NYT::FromProto<TTransactionId>(request->transaction_id()),
            transactionAttachOptions);
        if (!transaction) {
            return;
        }

        // TODO(sandello): Options!
        transaction->Abort().Subscribe(BIND([=] (const TErrorOr<void>& result) {
            if (!result.IsOK()) {
                context->Reply(result);
            } else {
                context->Reply();
            }
        }));
    }

    ////////////////////////////////////////////////////////////////////////////////
    // OPTIONS
    ////////////////////////////////////////////////////////////////////////////////

    static void SetTimeoutOptions(
        TTimeoutOptions* options,
        IServiceContext* context)
    {
        options->Timeout = context->GetTimeout();
    }

    static void FromProto(
        TTransactionalOptions* options,
        const NProto::TTransactionalOptions& proto)
    {
        if (proto.has_transaction_id()) {
            NYT::FromProto(&options->TransactionId, proto.transaction_id());
        }
        if (proto.has_ping()) {
            options->Ping = proto.ping();
        }
        if (proto.has_ping_ancestors()) {
            options->PingAncestors = proto.ping_ancestors();
        }
        if (proto.has_sticky()) {
            options->Sticky = proto.sticky();
        }
    }

    static void FromProto(
        TPrerequisiteOptions* options,
        const NProto::TPrerequisiteOptions& proto)
    {
        options->PrerequisiteTransactionIds.resize(proto.transactions_size());
        for (int i = 0; i < proto.transactions_size(); ++i) {
            const auto& protoItem = proto.transactions(i);
            auto& item = options->PrerequisiteTransactionIds[i];
            NYT::FromProto(&item, protoItem.transaction_id());
        }
        options->PrerequisiteRevisions.reserve(proto.revisions_size());
        for (int i = 0; i < proto.revisions_size(); ++i) {
            const auto& protoItem = proto.revisions(i);
            options->PrerequisiteRevisions[i] = New<TPrerequisiteRevisionConfig>();
            auto& item = *options->PrerequisiteRevisions[i];
            NYT::FromProto(&item.TransactionId, protoItem.transaction_id());
            item.Revision = protoItem.revision();
            item.Path = protoItem.path();
        }
    }

    static void FromProto(
        TMasterReadOptions* options,
        const NProto::TMasterReadOptions& proto)
    {
        if (proto.has_read_from()) {
            switch (proto.read_from()) {
                case NProto::TMasterReadOptions_EMasterReadKind_LEADER:
                    options->ReadFrom = EMasterChannelKind::Leader;
                    break;
                case NProto::TMasterReadOptions_EMasterReadKind_FOLLOWER:
                    options->ReadFrom = EMasterChannelKind::Follower;
                    break;
                case NProto::TMasterReadOptions_EMasterReadKind_CACHE:
                    options->ReadFrom = EMasterChannelKind::Cache;
                    break;
            }
        }
        if (proto.has_success_expiration_time()) {
            NYT::FromProto(&options->ExpireAfterSuccessfulUpdateTime, proto.success_expiration_time());
        }
        if (proto.has_failure_expiration_time()) {
            NYT::FromProto(&options->ExpireAfterFailedUpdateTime, proto.failure_expiration_time());
        }
        if (proto.has_cache_sticky_group_size()) {
            options->CacheStickyGroupSize = proto.cache_sticky_group_size();
        }
    }

    static void FromProto(
        TMutatingOptions* options,
        const NProto::TMutatingOptions& proto)
    {
        if (proto.has_mutation_id()) {
            NYT::FromProto(&options->MutationId, proto.mutation_id());
        }
        if (proto.has_retry()) {
            options->Retry = proto.retry();
        }
    }

    static void FromProto(
        TSuppressableAccessTrackingOptions* options,
        const NProto::TSuppressableAccessTrackingOptions& proto)
    {
        if (proto.has_suppress_access_tracking()) {
            options->SuppressAccessTracking = proto.suppress_access_tracking();
        }
        if (proto.has_suppress_modification_tracking()) {
            options->SuppressModificationTracking = proto.suppress_modification_tracking();
        }
    }

    static void FromProto(
        TTabletRangeOptions* options,
        const NProto::TTabletRangeOptions& proto)
    {
        if (proto.has_first_tablet_index()) {
            options->FirstTabletIndex = proto.first_tablet_index();
        }
        if (proto.has_last_tablet_index()) {
            options->LastTabletIndex = proto.last_tablet_index();
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // CYPRESS
    ////////////////////////////////////////////////////////////////////////////////

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, ExistsNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TNodeExistsOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_master_read_options()) {
            FromProto(&options, request->master_read_options());
        }
        if (request->has_suppressable_access_tracking_options()) {
            FromProto(&options, request->suppressable_access_tracking_options());
        }

        client->NodeExists(path, options)
            .Subscribe(BIND([=] (const TErrorOr<bool>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    response->set_exists(result.Value());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, GetNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();
        TGetNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_attributes()) {
            const auto& protoAttributes = request->attributes();
            if (protoAttributes.all()) {
                options.Attributes.Reset();
            } else {
                options.Attributes = std::vector<Stroka>();
                options.Attributes->reserve(protoAttributes.columns_size());
                for (int i = 0; i < protoAttributes.columns_size(); ++i) {
                    const auto& protoItem = protoAttributes.columns(i);
                    options.Attributes->push_back(protoItem);
                }
            }
        }
        if (request->has_max_size()) {
            options.MaxSize = request->max_size();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_master_read_options()) {
            FromProto(&options, request->master_read_options());
        }
        if (request->has_suppressable_access_tracking_options()) {
            FromProto(&options, request->suppressable_access_tracking_options());
        }

        client->GetNode(path, options)
            .Subscribe(BIND([=] (const TErrorOr<NYson::TYsonString>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    response->set_value(result.Value().GetData());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, ListNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TListNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_attributes()) {
            const auto& protoAttributes = request->attributes();
            if (protoAttributes.all()) {
                options.Attributes.Reset();
            } else {
                options.Attributes = std::vector<Stroka>();
                options.Attributes->reserve(protoAttributes.columns_size());
                for (int i = 0; i < protoAttributes.columns_size(); ++i) {
                    const auto& protoItem = protoAttributes.columns(i);
                    options.Attributes->push_back(protoItem);
                }
            }
        }
        if (request->has_max_size()) {
            options.MaxSize = request->max_size();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_master_read_options()) {
            FromProto(&options, request->master_read_options());
        }
        if (request->has_suppressable_access_tracking_options()) {
            FromProto(&options, request->suppressable_access_tracking_options());
        }

        client->ListNode(path, options)
            .Subscribe(BIND([=] (const TErrorOr<NYson::TYsonString>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    response->set_value(result.Value().GetData());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, CreateNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();
        NObjectClient::EObjectType type = static_cast<NObjectClient::EObjectType>(request->type());

        TCreateNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_attributes()) {
            const auto& protoAttributes = request->attributes();
            auto attributes = std::shared_ptr<IAttributeDictionary>(CreateEphemeralAttributes());
            for (int i = 0; i < protoAttributes.attributes_size(); ++i) {
                const auto& protoItem = protoAttributes.attributes(i);
                attributes->SetYson(protoItem.key(), NYson::TYsonString(protoItem.value()));
            }
            options.Attributes = std::move(attributes);
        }
        if (request->has_recursive()) {
            options.Recursive = request->recursive();
        }
        if (request->has_force()) {
            options.Force = request->force();
        }
        if (request->has_ignore_existing()) {
            options.IgnoreExisting = request->ignore_existing();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->CreateNode(path, type, options)
            .Subscribe(BIND([=] (const TErrorOr<NCypressClient::TNodeId>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    ToProto(response->mutable_node_id(), result.Value());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, RemoveNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TRemoveNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_recursive()) {
            options.Recursive = request->recursive();
        }
        if (request->has_force()) {
            options.Force = request->force();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->RemoveNode(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, SetNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();
        NYson::TYsonString value(request->value());

        TSetNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->SetNode(path, value, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, LockNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();
        NCypressClient::ELockMode mode = static_cast<NCypressClient::ELockMode>(request->mode());

        TLockNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_waitable()) {
            options.Waitable = request->waitable();
        }
        if (request->has_child_key()) {
            options.ChildKey = request->child_key();
        }
        if (request->has_attribute_key()) {
            options.AttributeKey = request->attribute_key();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->LockNode(path, mode, options)
            .Subscribe(BIND([=] (const TErrorOr<TLockNodeResult>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    ToProto(response->mutable_node_id(), result.Value().NodeId);
                    ToProto(response->mutable_lock_id(), result.Value().LockId);
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, CopyNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& srcPath = request->src_path();
        auto&& dstPath = request->dst_path();

        TCopyNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_recursive()) {
            options.Recursive = request->recursive();
        }
        if (request->has_force()) {
            options.Force = request->force();
        }
        if (request->has_preserve_account()) {
            options.PreserveAccount = request->preserve_account();
        }
        if (request->has_preserve_expiration_time()) {
            options.PreserveExpirationTime = request->preserve_expiration_time();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->CopyNode(srcPath, dstPath, options)
            .Subscribe(BIND([=] (const TErrorOr<NCypressClient::TNodeId>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    ToProto(response->mutable_node_id(), result.Value());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, MoveNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& srcPath = request->src_path();
        auto&& dstPath = request->dst_path();

        TMoveNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_recursive()) {
            options.Recursive = request->recursive();
        }
        if (request->has_force()) {
            options.Force = request->force();
        }
        if (request->has_preserve_account()) {
            options.PreserveAccount = request->preserve_account();
        }
        if (request->has_preserve_expiration_time()) {
            options.PreserveExpirationTime = request->preserve_expiration_time();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->MoveNode(srcPath, dstPath, options)
            .Subscribe(BIND([=] (const TErrorOr<NCypressClient::TNodeId>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    ToProto(response->mutable_node_id(), result.Value());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, LinkNode)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& srcPath = request->src_path();
        auto&& dstPath = request->dst_path();

        TLinkNodeOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_recursive()) {
            options.Recursive = request->recursive();
        }
        if (request->has_force()) {
            options.Force = request->force();
        }
        if (request->has_ignore_existing()) {
            options.IgnoreExisting = request->ignore_existing();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        if (request->has_prerequisite_options()) {
            FromProto(&options, request->prerequisite_options());
        }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->LinkNode(srcPath, dstPath, options)
            .Subscribe(BIND([=] (const TErrorOr<NCypressClient::TNodeId>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    ToProto(response->mutable_node_id(), result.Value());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, ConcatenateNodes)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        std::vector<NYPath::TYPath> srcPaths;
        srcPaths.reserve(request->src_path_size());
        for (int i = 0; i < request->src_path_size(); ++i) {
            srcPaths.push_back(request->src_path(i));
        }
        auto&& dstPath = request->dst_path();

        TConcatenateNodesOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_append()) {
            options.Append = request->append();
        }

        if (request->has_transactional_options()) {
            FromProto(&options, request->transactional_options());
        }
        // if (request->has_prerequisite_options()) {
        //     FromProto(&options, request->prerequisite_options());
        // }
        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }

        client->ConcatenateNodes(srcPaths, dstPath, options)
            .Subscribe(BIND([=] (const TErrorOr<NCypressClient::TNodeId>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    ////////////////////////////////////////////////////////////////////////////////
    // TABLES (NON-TRANSACTIONAL)
    ////////////////////////////////////////////////////////////////////////////////

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, MountTable)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TMountTableOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_cell_id()) {
            NYT::FromProto(&options.CellId, request->cell_id());
        }
        if (request->has_freeze()) {
            options.Freeze = request->freeze();
        }

        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }
        if (request->has_tablet_range_options()) {
            FromProto(&options, request->tablet_range_options());
        }

        client->MountTable(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, UnmountTable)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TUnmountTableOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_force()) {
            options.Force = request->force();
        }

        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }
        if (request->has_tablet_range_options()) {
            FromProto(&options, request->tablet_range_options());
        }

        client->UnmountTable(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, RemountTable)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TRemountTableOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }
        if (request->has_tablet_range_options()) {
            FromProto(&options, request->tablet_range_options());
        }

        client->RemountTable(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, FreezeTable)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TFreezeTableOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }
        if (request->has_tablet_range_options()) {
            FromProto(&options, request->tablet_range_options());
        }

        client->FreezeTable(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, UnfreezeTable)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        auto&& path = request->path();

        TUnfreezeTableOptions options;
        SetTimeoutOptions(&options, context.Get());

        if (request->has_mutating_options()) {
            FromProto(&options, request->mutating_options());
        }
        if (request->has_tablet_range_options()) {
            FromProto(&options, request->tablet_range_options());
        }

        client->UnfreezeTable(path, options)
            .Subscribe(BIND([=] (const TErrorOr<void>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, ReshardTable)
    {
        Y_UNREACHABLE();
    }

    ////////////////////////////////////////////////////////////////////////////////
    // TABLES (TRANSACTIONAL)
    ////////////////////////////////////////////////////////////////////////////////

    template <class TContext, class TRequest, class TOptions>
    static bool LookupRowsPrologue(
        const TIntrusivePtr<TContext>& context,
        TRequest* request,
        const NProto::TRowsetDescriptor& rowsetDescriptor,
        TNameTablePtr* nameTable,
        TSharedRange<TUnversionedRow>* keys,
        TOptions* options)
    {
        ValidateRowsetDescriptor(request->rowset_descriptor(), 1, NProto::ERowsetKind::UNVERSIONED);
        if (request->Attachments().empty()) {
            context->Reply(TError("Request is missing data"));
            return false;
        }

        auto rowset = DeserializeRowset<TUnversionedRow>(
            request->rowset_descriptor(),
            MergeRefsToRef<TApiServiceBufferTag>(request->Attachments()));
        *nameTable = TNameTable::FromSchema(rowset->Schema());
        *keys = MakeSharedRange(rowset->GetRows(), rowset);

        options->Timeout = context->GetTimeout();
        for (int i = 0; i < request->columns_size(); ++i) {
            options->ColumnFilter.All = false;
            options->ColumnFilter.Indexes.push_back((*nameTable)->GetIdOrRegisterName(request->columns(i)));
        }
        options->Timestamp = request->timestamp();
        options->KeepMissingRows = request->keep_missing_rows();

        context->SetRequestInfo("Path: %v, Rows: %v", request->path(), keys->Size());

        return true;
    }

    template <class TContext, class TResponse, class TRow>
    static void LookupRowsEpilogue(
        const TIntrusivePtr<TContext>& context,
        TResponse* response,
        const TErrorOr<TIntrusivePtr<IRowset<TRow>>>& result)
    {
        if (!result.IsOK()) {
            context->Reply(result);
        } else {
            const auto& value = result.Value();
            response->Attachments() = SerializeRowset(
                value->Schema(),
                value->GetRows(),
                response->mutable_rowset_descriptor());
            context->Reply();
        }
    };

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, LookupRows)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        TNameTablePtr nameTable;
        TSharedRange<TUnversionedRow> keys;
        TLookupRowsOptions options;

        if (!LookupRowsPrologue(context, request, request->rowset_descriptor(), &nameTable, &keys, &options)) {
            return;
        }

        client->LookupRows(request->path(), std::move(nameTable), std::move(keys), options)
            .Subscribe(BIND([=] (const TErrorOr<IUnversionedRowsetPtr>& result) {
                LookupRowsEpilogue(context, response, result);
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, VersionedLookupRows)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        TNameTablePtr nameTable;
        TSharedRange<TUnversionedRow> keys;
        TVersionedLookupRowsOptions options;

        if (!LookupRowsPrologue(context, request, request->rowset_descriptor(), &nameTable, &keys, &options)) {
            return;
        }

        client->VersionedLookupRows(request->path(), std::move(nameTable), std::move(keys), options)
            .Subscribe(BIND([=] (const TErrorOr<IVersionedRowsetPtr>& result) {
                LookupRowsEpilogue(context, response, result);
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, SelectRows)
    {
        auto client = GetAuthenticatedClientOrAbortContext(context, request);
        if (!client) {
            return;
        }

        TSelectRowsOptions options; // TODO: Fill all options.
        options.Timestamp = NTransactionClient::AsyncLastCommittedTimestamp;

        client->SelectRows(request->query(), options)
            .Subscribe(BIND([=] (const TErrorOr<TSelectRowsResult>& result) {
                if (!result.IsOK()) {
                    context->Reply(result);
                } else {
                    const auto& value = result.Value();
                    response->Attachments() = SerializeRowset(
                        value.Rowset->Schema(),
                        value.Rowset->GetRows(),
                        response->mutable_rowset_descriptor());
                    context->Reply();
                }
            }));
    }

    DECLARE_RPC_SERVICE_METHOD(NRpcProxy::NProto, ModifyRows)
    {
        auto transactionAttachOptions = TTransactionAttachOptions{};
        transactionAttachOptions.Ping = false;
        transactionAttachOptions.PingAncestors = false;
        transactionAttachOptions.Sticky = true; // XXX(sandello): Fix me!
        auto transaction = GetTransactionOrAbortContext(
            context,
            request,
            NYT::FromProto<TTransactionId>(request->transaction_id()),
            transactionAttachOptions);
        if (!transaction) {
            return;
        }

        auto rowset = DeserializeRowset<TUnversionedRow>(
            request->rowset_descriptor(),
            MergeRefsToRef<TApiServiceBufferTag>(request->Attachments()));

        const auto& rowsetRows = rowset->GetRows();
        auto rowsetSize = rowset->GetRows().Size();

        if (rowsetSize != request->row_modification_types_size()) {
            THROW_ERROR_EXCEPTION("Row count mismatch");
        }

        std::vector<TRowModification> modifications;
        modifications.reserve(rowsetSize);
        for (size_t index = 0; index < rowsetSize; ++index) {
            modifications.push_back({
                ERowModificationType(request->row_modification_types(index)),
                rowsetRows[index]
            });
        }

        TModifyRowsOptions options;
        transaction->ModifyRows(
            request->path(),
            TNameTable::FromSchema(rowset->Schema()),
            MakeSharedRange(std::move(modifications), rowset),
            options);

        context->Reply();
    }

};

IServicePtr CreateApiService(
    NCellProxy::TBootstrap* bootstrap)
{
    return New<TApiService>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpcProxy
} // namespace NYT

