#include "stdafx.h"
#include "virtual.h"
#include "fluent.h"
#include "node_detail.h"
#include "yson_writer.h"
#include "ypath_detail.h"
#include "ypath_client.h"

#include <ytlib/misc/configurable.h>

namespace NYT {
namespace NYTree {

using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

IYPathService::TResolveResult TVirtualMapBase::ResolveRecursive(const TYPath& path, const Stroka& verb)
{
    UNUSED(verb);

    Stroka token;
    TYPath suffixPath;
    ChopYPathToken(path, &token, &suffixPath);

    auto service = GetItemService(token);
    if (!service) {
        ythrow yexception() << Sprintf("Key %s is not found", ~token.Quote());
    }

    return TResolveResult::There(~service, suffixPath);
}

void TVirtualMapBase::DoInvoke(NRpc::IServiceContext* context)
{
    DISPATCH_YPATH_SERVICE_METHOD(Get);
    TYPathServiceBase::DoInvoke(context);
}

struct TGetConfig
    : public TConfigurable
{
    int MaxSize;

    TGetConfig()
    {
        Register("max_size", MaxSize)
            .GreaterThanOrEqual(0)
            .Default(100);
    }
};

DEFINE_RPC_SERVICE_METHOD(TVirtualMapBase, Get)
{
    if (!IsFinalYPath(context->GetPath())) {
        ythrow yexception() << "Path must be final";
    }

    auto config = New<TGetConfig>();
    if (request->has_options()) {
        auto options = DeserializeFromYson(request->options());
        config->Load(~options);
    }
    config->Validate();
    
    TStringStream stream;
    TYsonWriter writer(&stream, EYsonFormat::Binary);
    auto keys = GetKeys(config->MaxSize);
    auto size = GetSize();

    // TODO(MRoizner): use fluent
    BuildYsonFluently(&writer);
    writer.OnBeginMap();
    FOREACH (const auto& key, keys) {
        writer.OnMapItem(key);
        writer.OnEntity(false);
    }

    bool incomplete = keys.ysize() != size;
    writer.OnEndMap(incomplete);
    if (incomplete) {
        writer.OnBeginAttributes();
        writer.OnAttributesItem("incomplete");
        writer.OnStringScalar("true");
        writer.OnEndAttributes();
    }

    response->set_value(stream.Str());
    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualEntityNode
    : public TNodeBase
    , public IEntityNode
{
    YTREE_NODE_TYPE_OVERRIDES(Entity)

public:
    TVirtualEntityNode(TYPathServiceProvider* builder)
        : Provider(builder)
    { }

    virtual INodeFactory::TPtr CreateFactory() const
    {
        YASSERT(Parent);
        return Parent->CreateFactory();
    }

    virtual ICompositeNode::TPtr GetParent() const
    {
        return Parent;
    }

    virtual void SetParent(ICompositeNode* parent)
    {
        Parent = parent;
    }

    virtual TResolveResult Resolve(const TYPath& path, const Stroka& verb)
    {
        if (IsLocalYPath(path)) {
            return TNodeBase::Resolve(path, verb);
        } else {
            auto service = Provider->Do();
            return TResolveResult::There(~service, path);
        }
    }

private:
    TYPathServiceProvider::TPtr Provider;
    ICompositeNode* Parent;

};

INode::TPtr CreateVirtualNode(TYPathServiceProvider* provider)
{
    return New<TVirtualEntityNode>(provider);
}

INode::TPtr CreateVirtualNode(IYPathService* service)
{
    IYPathService::TPtr service_ = service;
    return CreateVirtualNode(~FromFunctor([=] () -> NYTree::IYPathService::TPtr
        {
            return service_;
        }));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
