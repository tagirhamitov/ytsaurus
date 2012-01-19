#include "stdafx.h"
#include "yson_file_service.h"
#include "serialize.h"
#include "tree_builder.h"
#include "ephemeral.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

// TOOD: read-only?
class TYsonFileService
    : public IYPathService
{
public:
    TYsonFileService(const Stroka& fileName)
        : FileName(fileName)
    { }

    virtual TResolveResult Resolve(const TYPath& path, const Stroka& verb)
    {
        UNUSED(verb);

        auto node = LoadFile();
        return TResolveResult::There(~node, path);
    }

    virtual void Invoke(NRpc::IServiceContext* context)
    {
        UNUSED(context);
        ythrow yexception() << "Direct invocation is forbidden";
    }

    virtual Stroka GetLoggingCategory() const
    {
        return YTreeLogger.GetCategory();
    }

private:
    Stroka FileName;

    INode::TPtr LoadFile()
    {
        try {
            TIFStream stream(FileName);
           return DeserializeFromYson(&stream);
        } catch (...) {
            throw yexception() << Sprintf("Error loading YSON file %s\n%s",
                ~FileName,
                ~CurrentExceptionMessage());
        }
    }
};

TYPathServiceProvider::TPtr CreateYsonFileProvider(const Stroka& fileName)
{
    return FromFunctor([=] () -> IYPathService::TPtr
        {
            return New<TYsonFileService>(fileName);
        });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
