#include "server.h"
#include "dispatcher.h"
#include "config.h"
#include "helpers.h"

#include <yt/core/rpc/grpc/proto/grpc.pb.h>

#include <yt/core/rpc/server_detail.h>
#include <yt/core/rpc/message.h>
#include <yt/core/rpc/proto/rpc.pb.h>

#include <yt/core/bus/bus.h>

#include <yt/core/misc/small_vector.h>

#include <yt/core/ytree/convert.h>

#include <contrib/libs/grpc/include/grpc/grpc.h>
#include <contrib/libs/grpc/include/grpc/grpc_security.h>
#include <contrib/libs/grpc/include/grpc/impl/codegen/grpc_types.h>
#include <contrib/libs/grpc/include/grpcpp/support/slice.h>

#include <array>

template <>
void Out<grpc_slice>(IOutputStream& o, const grpc_slice& p)
{
    o.Write(GRPC_SLICE_START_PTR(p), GRPC_SLICE_LENGTH(p));
}

namespace NYT {
namespace NRpc {
namespace NGrpc {

using namespace NRpc;
using namespace NBus;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TServer)

DEFINE_ENUM(EServerCallStage,
    (Accept)
    (ReceivingRequest)
    (SendingInitialMetadata)
    (WaitingForService)
    (SendingResponse)
    (WaitingForClose)
);

DEFINE_ENUM(EServerCallCookie,
    (Normal)
    (Close)
);

class TServer
    : public TServerBase
{
public:
    explicit TServer(TServerConfigPtr config)
        : TServerBase(NLogging::TLogger(GrpcLogger)
            .AddTag("ServerId: %v", TGuid::Create()))
        , Config_(std::move(config))
        , LibraryLock_(TDispatcher::Get()->CreateLibraryLock())
        , CompletionQueue_(TDispatcher::Get()->PickRandomCompletionQueue())
    { }

private:
    const TServerConfigPtr Config_;

    const TGrpcLibraryLockPtr LibraryLock_;
    grpc_completion_queue* const CompletionQueue_;

    TGrpcServerPtr Native_;
    std::vector<TGrpcServerCredentialsPtr> Credentials_;


    virtual void DoStart() override
    {
        TGrpcChannelArgs args(Config_->GrpcArguments);

        Native_ = TGrpcServerPtr(grpc_server_create(
            args.Unwrap(),
            nullptr));

        grpc_server_register_completion_queue(
            Native_.Unwrap(),
            CompletionQueue_,
            nullptr);

        try {
            for (const auto& addressConfig : Config_->Addresses) {
                int result;
                if (addressConfig->Credentials) {
                    Credentials_.push_back(LoadServerCredentials(addressConfig->Credentials));
                    result = grpc_server_add_secure_http2_port(
                        Native_.Unwrap(),
                        addressConfig->Address.c_str(),
                        Credentials_.back().Unwrap());
                } else {
                    result = grpc_server_add_insecure_http2_port(
                        Native_.Unwrap(),
                        addressConfig->Address.c_str());
                }
                if (result == 0) {
                    THROW_ERROR_EXCEPTION("Error configuring server to listen at %Qv",
                        addressConfig->Address);
                }
                LOG_DEBUG("Server address configured (Address: %v)", addressConfig->Address);
            }
        } catch (const std::exception& ex) {
            Cleanup();
            throw;
        }

        grpc_server_start(Native_.Unwrap());

        Ref();

        TServerBase::DoStart();

        New<TCallHandler>(this);
    }

    virtual TFuture<void> DoStop(bool graceful) override
    {
        class TStopTag
            : public TCompletionQueueTag
        {
        public:
            TFuture<void> GetFuture()
            {
                return Promise_.ToFuture();
            }

            virtual void Run(bool success, int /*cookie*/) override
            {
                Promise_.Set(success ? TError() : TError("GRPC server shutdown failed"));
                delete this;
            }

        private:
            TPromise<void> Promise_ = NewPromise<void>();

        };

        auto* shutdownTag = graceful ? new TStopTag() : nullptr;
        auto shutdownFuture = shutdownTag ? shutdownTag->GetFuture() : VoidFuture;

        grpc_server_shutdown_and_notify(
            Native_.Unwrap(),
            CompletionQueue_,
            shutdownTag ? shutdownTag->GetTag() : nullptr);

        if (!graceful) {
            grpc_server_cancel_all_calls(Native_.Unwrap());
        }

        return shutdownFuture.Apply(BIND(&TServer::OnShutdownFinished, MakeStrong(this), graceful));
    }

    TFuture<void> OnShutdownFinished(bool graceful)
    {
        Cleanup();
        Unref();
        return TServerBase::DoStop(graceful);
    }

    void Cleanup()
    {
        Native_.Reset();
    }

    class TCallHandler
        : public TCompletionQueueTag
        , public IBus
    {
    public:
        explicit TCallHandler(TServerPtr owner)
            : Owner_(std::move(owner))
            , CompletionQueue_(TDispatcher::Get()->PickRandomCompletionQueue())
            , Logger(Owner_->Logger)
        {
            auto result = grpc_server_request_call(
                Owner_->Native_.Unwrap(),
                Call_.GetPtr(),
                CallDetails_.Unwrap(),
                CallMetadata_.Unwrap(),
                CompletionQueue_,
                Owner_->CompletionQueue_,
                GetTag());
            YCHECK(result == GRPC_CALL_OK);

            EndpointDescription_ = grpc::StringFromCopiedSlice(CallDetails_->host);

            Ref();
        }

        // TCompletionQueueTag overrides
        virtual void Run(bool success, int cookie_) override
        {
            auto cookie = EServerCallCookie(cookie_);
            switch (cookie) {
                case EServerCallCookie::Normal:
                    switch (Stage_) {
                        case EServerCallStage::Accept:
                            OnAccepted(success);
                            break;

                        case EServerCallStage::ReceivingRequest:
                            OnRequestReceived(success);
                            break;

                        case EServerCallStage::SendingInitialMetadata:
                            OnInitialMetadataSent(success);
                            break;

                        case EServerCallStage::SendingResponse:
                            OnResponseSent(success);
                            break;

                        default:
                            Y_UNREACHABLE();
                    }
                    break;

                case EServerCallCookie::Close:
                    OnCloseReceived(success);
                    break;

                default:
                    Y_UNREACHABLE();
            }
        }

        // IBus overrides
        virtual const TString& GetEndpointDescription() const override
        {
            return EndpointDescription_;
        }

        virtual const NYTree::IAttributeDictionary& GetEndpointAttributes() const override
        {
            Y_UNREACHABLE();
        }

        virtual TTcpDispatcherStatistics GetStatistics() const override
        {
            return {};
        }

        virtual TFuture<void> Send(TSharedRefArray message, const NBus::TSendOptions& /*options*/) override
        {
            auto guard = Guard(SpinLock_);
            YCHECK(!ResponseMessage_);
            ResponseMessage_ = std::move(message);
            MaybeSendResponse(guard);
            return TFuture<void>();
        }

        virtual void SetTosLevel(TTosLevel /*tosLevel*/) override
        { }

        virtual void Terminate(const TError& error) override
        { }

        virtual void SubscribeTerminated(const TCallback<void(const TError&)>& callback) override
        { }

        virtual void UnsubscribeTerminated(const TCallback<void(const TError&)>& callback) override
        { }

    private:
        const TServerPtr Owner_;

        grpc_completion_queue* const CompletionQueue_;
        const NLogging::TLogger& Logger;

        TSpinLock SpinLock_;
        EServerCallStage Stage_ = EServerCallStage::Accept;
        TSharedRefArray ResponseMessage_;

        TRequestId RequestId_;
        TString ServiceName_;
        TString MethodName_;
        TNullable<TString> PeerIdentity_;
        TNullable<TDuration> Timeout_;
        IServicePtr Service_;

        TString EndpointDescription_;

        TGrpcMetadataArrayBuilder InitialMetadataBuilder_;
        TGrpcMetadataArrayBuilder TrailingMetadataBuilder_;

        TGrpcCallDetails CallDetails_;
        TGrpcMetadataArray CallMetadata_;
        TGrpcCallPtr Call_;
        TGrpcByteBufferPtr RequestBodyBuffer_;
        TGrpcByteBufferPtr ResponseBodyBuffer_;
        TString ErrorMessage_;
        grpc_slice ErrorMessageSlice_ = grpc_empty_slice();
        int Canceled_ = 0;


        template <class TOps>
        void StartBatch(const TOps& ops, EServerCallCookie cookie)
        {
            auto result = grpc_call_start_batch(
                Call_.Unwrap(),
                ops.data(),
                ops.size(),
                GetTag(static_cast<int>(cookie)),
                nullptr);
            YCHECK(result == GRPC_CALL_OK);
        }

        void OnAccepted(bool success)
        {
            if (!success) {
                // This normally happens on server shutdown.
                LOG_DEBUG("Server accept failed");
                Unref();
                return;
            }

            New<TCallHandler>(Owner_);

            ParseRequestId();

            if (!ParseRoutingParameters()) {
                LOG_DEBUG("Malformed request routing parameters (RawMethod: %v, RequestId: %v)",
                    CallDetails_->method,
                    RequestId_);
                Unref();
                return;
            }

            ParseAuthParameters();

            ParseTimeout();

            LOG_DEBUG("Request accepted (RequestId: %v, Method: %v:%v, PeerIdentity: %v, PeerAddress: %v, Timeout: %v)",
                RequestId_,
                ServiceName_,
                MethodName_,
                PeerIdentity_,
                TStringBuf(GetPeerAddress().get()),
                Timeout_);

            Service_ = Owner_->FindService(TServiceId(ServiceName_));

            {
                auto guard = Guard(SpinLock_);
                Stage_ = EServerCallStage::ReceivingRequest;
            }

            std::array<grpc_op, 1> ops;

            ops[0].op = GRPC_OP_RECV_MESSAGE;
            ops[0].flags = 0;
            ops[0].reserved = nullptr;
            ops[0].data.recv_message.recv_message = RequestBodyBuffer_.GetPtr();

            StartBatch(ops, EServerCallCookie::Normal);
        }

        void ParseRequestId()
        {
            auto idString = CallMetadata_.Find(RequestIdMetadataKey);
            if (!idString) {
                RequestId_ = TRequestId::Create();
                return;
            }

            if (!TRequestId::FromString(idString, &RequestId_)) {
                RequestId_ = TRequestId::Create();
                LOG_WARNING("Malformed request id, using a random one (MalformedRequestId: %v, RequestId: %v)",
                    idString,
                    RequestId_);
            }
        }

        void ParseAuthParameters()
        {
            auto authContext = TGrpcAuthContextPtr(grpc_call_auth_context(Call_.Unwrap()));
            if (!authContext) {
                return;
            }

            const char* peerIdentityPropertyName = grpc_auth_context_peer_identity_property_name(authContext.Unwrap());
            if (!peerIdentityPropertyName) {
                return;
            }

            auto peerIdentityPropertyIt = grpc_auth_context_find_properties_by_name(authContext.Unwrap(), peerIdentityPropertyName);
            auto* peerIdentityProperty = grpc_auth_property_iterator_next(&peerIdentityPropertyIt);
            if (!peerIdentityProperty) {
                return;
            }

            PeerIdentity_ = TString(peerIdentityProperty->value);
        }

        void ParseTimeout()
        {
            auto deadline = CallDetails_->deadline;
            deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_REALTIME);
            auto now = gpr_now(GPR_CLOCK_REALTIME);
            if (gpr_time_cmp(now, deadline) >= 0) {
                Timeout_ = TDuration::Zero();
                return;
            }

            auto micros = gpr_timespec_to_micros(gpr_time_sub(deadline, now));
            if (micros > std::numeric_limits<ui64>::max() / 2) {
                return;
            }

            Timeout_ = TDuration::MicroSeconds(static_cast<ui64>(micros));
        }

        bool ParseRoutingParameters()
        {
            if (*GRPC_SLICE_START_PTR(CallDetails_->method) != '/') {
                return false;
            }

            const size_t methodLength = GRPC_SLICE_LENGTH(CallDetails_->method);
            auto methodWithoutLeadingSlash = grpc_slice_sub_no_ref(CallDetails_->method, 1, methodLength);
            const int secondSlashIndex = grpc_slice_chr(methodWithoutLeadingSlash, '/');
            if (secondSlashIndex < 0) {
                return false;
            }

            const char *serviceNameStart = reinterpret_cast<const char *>(GRPC_SLICE_START_PTR(methodWithoutLeadingSlash));
            ServiceName_.assign(serviceNameStart, secondSlashIndex);
            MethodName_.assign(serviceNameStart + secondSlashIndex + 1, methodLength - 1 - (secondSlashIndex + 1));
            return true;
        }

        TGprString GetPeerAddress()
        {
            return MakeGprString(grpc_call_get_peer(Call_.Unwrap()));
        }

        void OnRequestReceived(bool success)
        {
            if (!success) {
                LOG_DEBUG("Failed to receive request body (RequestId: %v)",
                    RequestId_);
                Unref();
                return;
            }

            if (!RequestBodyBuffer_) {
                LOG_DEBUG("Empty request body received (RequestId: %v)",
                    RequestId_);
                Unref();
                return;
            }

            auto requestBody = ByteBufferToEnvelopedMessage(RequestBodyBuffer_.Unwrap());

            auto header = std::make_unique<NRpc::NProto::TRequestHeader>();
            ToProto(header->mutable_request_id(), RequestId_);
            header->set_service(ServiceName_);
            header->set_method(MethodName_);
            header->set_protocol_version(GenericProtocolVersion);
            if (Timeout_) {
                header->set_timeout(ToProto<i64>(*Timeout_));
            }
            if (PeerIdentity_) {
                auto* ext = header->MutableExtension(NGrpc::NProto::TSslCredentialsExt::ssl_credentials_ext);
                ext->set_peer_identity(*PeerIdentity_);
            }

            {
                auto guard = Guard(SpinLock_);
                Stage_ = EServerCallStage::SendingInitialMetadata;
            }

            LOG_DEBUG("Request received (RequestId: %v)",
                RequestId_);

            InitialMetadataBuilder_.Add(RequestIdMetadataKey, ToString(RequestId_));

            {
                std::array<grpc_op, 1> ops;

                ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
                ops[0].flags = 0;
                ops[0].reserved = nullptr;
                ops[0].data.send_initial_metadata.maybe_compression_level.is_set = false;
                ops[0].data.send_initial_metadata.metadata = InitialMetadataBuilder_.Unwrap();
                ops[0].data.send_initial_metadata.count = InitialMetadataBuilder_.GetSize();

                StartBatch(ops, EServerCallCookie::Normal);
            }

            {
                Ref();

                std::array<grpc_op, 1> ops;

                ops[0].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
                ops[0].flags = 0;
                ops[0].reserved = nullptr;
                ops[0].data.recv_close_on_server.cancelled = &Canceled_;

                StartBatch(ops, EServerCallCookie::Close);
            }

            if (Service_) {
                auto requestMessage = CreateRequestMessage(*header, requestBody, {});
                Service_->HandleRequest(std::move(header), std::move(requestMessage), this);
            } else {
                auto error = TError(
                    NRpc::EErrorCode::NoSuchService,
                    "Service is not registered")
                    << TErrorAttribute("service", ServiceName_);
                LOG_WARNING(error);
                auto response = CreateErrorResponseMessage(RequestId_, error);
                Send(std::move(response), NBus::TSendOptions(EDeliveryTrackingLevel::None));
            }
        }

        void OnInitialMetadataSent(bool success)
        {
            if (!success) {
                LOG_DEBUG("Failed to send initial metadata (RequestId: %v)",
                    RequestId_);
                Unref();
                return;
            }

            {
                auto guard = Guard(SpinLock_);
                Stage_ = EServerCallStage::WaitingForService;
                MaybeSendResponse(guard);
            }
        }


        void MaybeSendResponse(TGuard<TSpinLock>& guard)
        {
            if (!ResponseMessage_) {
                return;
            }
            if (Stage_ != EServerCallStage::WaitingForService) {
                return;
            }
            Stage_ = EServerCallStage::SendingResponse;
            guard.Release();

            LOG_DEBUG("Sending response (RequestId: %v)",
                RequestId_);

            NRpc::NProto::TResponseHeader responseHeader;
            YCHECK(ParseResponseHeader(ResponseMessage_, &responseHeader));

            SmallVector<grpc_op, 2> ops;

            TError error;
            if (responseHeader.has_error() && responseHeader.error().code() != static_cast<int>(NYT::EErrorCode::OK)) {
                FromProto(&error, responseHeader.error());
                ErrorMessage_ = ToString(error);
                ErrorMessageSlice_ = grpc_slice_from_static_string(ErrorMessage_.c_str());
                TrailingMetadataBuilder_.Add(ErrorMetadataKey, SerializeError(error));
            } else {
                // Attachments are not supported.
                YCHECK(ResponseMessage_.Size() == 2);
                ResponseBodyBuffer_ = EnvelopedMessageToByteBuffer(ResponseMessage_[1]);

                ops.emplace_back();
                ops.back().op = GRPC_OP_SEND_MESSAGE;
                ops.back().data.send_message.send_message = ResponseBodyBuffer_.Unwrap();
                ops.back().flags = 0;
                ops.back().reserved = nullptr;
            }

            ops.emplace_back();
            ops.back().op = GRPC_OP_SEND_STATUS_FROM_SERVER;
            ops.back().flags = 0;
            ops.back().reserved = nullptr;
            ops.back().data.send_status_from_server.status = error.IsOK() ? GRPC_STATUS_OK : grpc_status_code(GenericErrorStatusCode);
            ops.back().data.send_status_from_server.status_details = error.IsOK() ? nullptr : &ErrorMessageSlice_;
            ops.back().data.send_status_from_server.trailing_metadata_count = TrailingMetadataBuilder_.GetSize();
            ops.back().data.send_status_from_server.trailing_metadata = TrailingMetadataBuilder_.Unwrap();

            StartBatch(ops, EServerCallCookie::Normal);
        }


        void OnResponseSent(bool success)
        {
            if (success) {
                LOG_DEBUG("Response sent (RequestId: %v)",
                    RequestId_);
            } else {
                LOG_DEBUG("Failed to send response (RequestId: %v)",
                    RequestId_);
            }

            Unref();
        }


        void OnCloseReceived(bool success)
        {
            if (success) {
                if (Canceled_) {
                    LOG_DEBUG("Request cancelation received (RequestId: %v)",
                        RequestId_);
                    if (Service_) {
                        Service_->HandleRequestCancelation(RequestId_);
                    }
                } else {
                    LOG_DEBUG("Request closed (RequestId: %v)",
                        RequestId_);
                }
            } else {
                LOG_DEBUG("Failed to close request (RequestId: %v)",
                    RequestId_);
            }

            Unref();
        }
    };
};

DEFINE_REFCOUNTED_TYPE(TServer)

IServerPtr CreateServer(TServerConfigPtr config)
{
    return New<TServer>(std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NGrpc
} // namespace NRpc
} // namespace NYT
