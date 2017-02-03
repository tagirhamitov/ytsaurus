#include "master.h"
#include "object.h"

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////


TMasterObject::TMasterObject(const TObjectId& id)
    : TNonversionedObjectBase(id)
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
