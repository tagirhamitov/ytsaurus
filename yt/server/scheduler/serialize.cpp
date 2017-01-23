#include "serialize.h"

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return 200200;
}

bool ValidateSnapshotVersion(int version)
{
    return version == 200100;
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

