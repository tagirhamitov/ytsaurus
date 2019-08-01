#pragma once

#include "public.h"

#include <yt/server/master/cell_master/public.h>

#include <yt/server/master/object_server/object_detail.h>

#include <yt/core/misc/property.h>
#include <yt/core/misc/ref_tracked.h>

namespace NYT::NNodeTrackerServer {

////////////////////////////////////////////////////////////////////////////////

class TDataCenter
    : public NObjectServer::TObject
    , public TRefTracked<TDataCenter>
{
public:
    DEFINE_BYVAL_RW_PROPERTY(TString, Name);

public:
    explicit TDataCenter(TDataCenterId id);

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
