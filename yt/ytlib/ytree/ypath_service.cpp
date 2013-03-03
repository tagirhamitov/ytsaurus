#include "stdafx.h"
#include "ypath_service.h"
#include "tree_builder.h"
#include "ephemeral_node_factory.h"
#include "ypath_client.h"
#include "ypath_detail.h"

namespace NYT {
namespace NYTree {

using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

IYPathServicePtr IYPathService::FromProducer(TYsonProducer producer)
{
    auto builder = CreateBuilderFromFactory(GetEphemeralNodeFactory());
    builder->BeginTree();
    producer.Run(~builder);
    return builder->EndTree();
}

////////////////////////////////////////////////////////////////////////////////

namespace {

class TViaYPathService
    : public TYPathServiceBase
{
public:
    TViaYPathService(
        IYPathServicePtr underlyingService,
        IInvokerPtr invoker)
        : UnderlyingService(underlyingService)
        , Invoker(invoker)
    { }

    virtual TResolveResult Resolve(
        const TYPath& path,
        IServiceContextPtr context) override
    {
        UNUSED(context);
        return TResolveResult::Here(path);
    }

private:
    IYPathServicePtr UnderlyingService;
    IInvokerPtr Invoker;

    virtual bool DoInvoke(IServiceContextPtr context) override
    {
        auto underlyingService = UnderlyingService;
        auto handler = BIND([=] () {
            ExecuteVerb(UnderlyingService, context);
        });
        auto wrappedHandler = context->Wrap(handler);
        bool result = Invoker->Invoke(wrappedHandler);
        if (!result) {
            context->Reply(TError(
                NRpc::EErrorCode::Unavailable,
                "Service unavailable"));
        }
        return true;
    }
};

} // namespace

IYPathServicePtr IYPathService::Via(IInvokerPtr invoker)
{
    return New<TViaYPathService>(this, invoker);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

class TFromProducerPathService
    : public TYPathServiceBase
{
public:
    explicit TFromProducerPathService(TYPathServiceProducer producer)
        : Producer(producer)
    { }

    virtual TResolveResult Resolve(
        const TYPath& path,
        IServiceContextPtr context) override
    {
        UNUSED(context);
        return TResolveResult::Here(path);
    }

private:
    TYPathServiceProducer Producer;

    virtual bool DoInvoke(IServiceContextPtr context) override
    {
        auto service = Producer.Run();
        ExecuteVerb(service, context);
        return true;
    }
};

} // namespace

IYPathServicePtr IYPathService::FromProducer(TYPathServiceProducer producer)
{
    return New<TFromProducerPathService>(producer);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
