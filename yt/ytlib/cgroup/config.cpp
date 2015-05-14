#include "stdafx.h"
#include "private.h"
#include "config.h"

namespace NYT {
namespace NCGroup {

////////////////////////////////////////////////////////////////////////////////

bool TCGroupConfig::IsCGroupSupported(const Stroka& cgroupType) const
{
    auto item = std::find_if(
        SupportedCGroups.begin(),
        SupportedCGroups.end(),
        [=] (const Stroka& type) {
            return type == cgroupType;
        });
    return (item != SupportedCGroups.end());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCGroup
} // namespace NYT