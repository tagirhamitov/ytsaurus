#pragma once

#include "common.h"
#include "ypath_service.h"
#include "ytree_rpc.pb.h"

#include "../misc/ref.h"
#include "../misc/property.h"
#include "../bus/message.h"
#include "../rpc/client.h"
#include "../rpc/message.h"
#include "../actions/action_util.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TYPathRequest;

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathRequest;

class TYPathResponse;

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathResponse;

////////////////////////////////////////////////////////////////////////////////

class TYPathRequest
    : public TRefCountedBase
{
    DECLARE_BYVAL_RO_PROPERTY(Verb, Stroka);
    DECLARE_BYVAL_RW_PROPERTY(Path, TYPath);
    DECLARE_BYREF_RW_PROPERTY(Attachments, yvector<TSharedRef>);

public:
    typedef TIntrusivePtr<TYPathRequest> TPtr;
    
    TYPathRequest(const Stroka& verb, TYPath path);

    NBus::IMessage::TPtr Serialize();

protected:
    virtual bool SerializeBody(TBlob* data) const = 0;

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathRequest
    : public TYPathRequest
    , public TRequestMessage
{
public:
    typedef TTypedYPathResponse<TRequestMessage, TResponseMessage> TTypedResponse;
    typedef TIntrusivePtr< TTypedYPathRequest<TRequestMessage, TResponseMessage> > TPtr;

    TTypedYPathRequest(const Stroka& verb, TYPath path)
        : TYPathRequest(verb, path)
    { }

protected:
    virtual bool SerializeBody(TBlob* data) const
    {
        return NRpc::SerializeMessage(this, data);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TYPathResponse
    : public TRefCountedBase
{
    DECLARE_BYREF_RW_PROPERTY(Attachments, yvector<TSharedRef>);
    DECLARE_BYVAL_RW_PROPERTY(Error, NRpc::TError);

public:
    typedef TIntrusivePtr<TYPathResponse> TPtr;

    void Deserialize(NBus::IMessage* message);

    NRpc::EErrorCode GetErrorCode() const;
    bool IsOK() const;

protected:
    virtual bool DeserializeBody(TRef data) = 0;

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathResponse
    : public TYPathResponse
    , public TResponseMessage
{
public:
    typedef TIntrusivePtr< TTypedYPathResponse<TRequestMessage, TResponseMessage> > TPtr;

protected:
    virtual bool DeserializeBody(TRef data)
    {
        return NRpc::DeserializeMessage(this, data);
    }

};

////////////////////////////////////////////////////////////////////////////////

#define YPATH_PROXY_METHOD(ns, method) \
    typedef ::NYT::NYTree::TTypedYPathRequest<ns::TReq##method, ns::TRsp##method> TReq##method; \
    typedef ::NYT::NYTree::TTypedYPathResponse<ns::TReq##method, ns::TRsp##method> TRsp##method; \
    \
    static TReq##method::TPtr method(TYPath path) \
    { \
        return New<TReq##method>(#method, path); \
    }

////////////////////////////////////////////////////////////////////////////////

class TYPathProxy
{
public:
    YPATH_PROXY_METHOD(NProto, Get);
    YPATH_PROXY_METHOD(NProto, Set);
    YPATH_PROXY_METHOD(NProto, Remove);

};

////////////////////////////////////////////////////////////////////////////////

//! Executes a YPath verb against a local service.
template <class TTypedRequest>
TIntrusivePtr< TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> > >
ExecuteYPath(IYPathService* rootService, TTypedRequest* request);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
