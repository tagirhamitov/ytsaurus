#include "serialize.h"

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return 300031;
}

bool ValidateSnapshotVersion(int version)
{
    return version >= 300030 && version <= GetCurrentSnapshotVersion();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
