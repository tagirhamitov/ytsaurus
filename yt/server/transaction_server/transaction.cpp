#include "transaction.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/serialize.h>

#include <yt/server/security_server/account.h>

#include <yt/core/misc/string.h>

#include <yt/core/ytree/fluent.h>

namespace NYT {
namespace NTransactionServer {

using namespace NYTree;
using namespace NYson;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

void TTransaction::TExportEntry::Persist(NCellMaster::TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Object);
    Persist(context, DestinationCellTag);
}

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(const TTransactionId& id)
    : TTransactionBase(id)
    , AccountingEnabled_(true)
    , Parent_(nullptr)
    , StartTime_(TInstant::Zero())
    , Acd_(this)
{ }

void TTransaction::Save(NCellMaster::TSaveContext& context) const
{
    TNonversionedObjectBase::Save(context);

    using NYT::Save;
    Save(context, GetPersistentState());
    Save(context, Timeout_);
    Save(context, AccountingEnabled_);
    Save(context, Title_);
    Save(context, NestedTransactions_);
    Save(context, Parent_);
    Save(context, StartTime_);
    Save(context, StagedObjects_);
    Save(context, ExportedObjects_);
    Save(context, ImportedObjects_);
    Save(context, LockedNodes_);
    Save(context, Locks_);
    Save(context, BranchedNodes_);
    Save(context, StagedNodes_);
    Save(context, AccountResourceUsage_);
    Save(context, Acd_);
}

void TTransaction::Load(NCellMaster::TLoadContext& context)
{
    TNonversionedObjectBase::Load(context);

    // COMPAT(babenko)
    YCHECK(context.GetVersion() >= 200);

    using NYT::Load;
    Load(context, State_);
    Load(context, Timeout_);
    Load(context, AccountingEnabled_);
    Load(context, Title_);
    Load(context, NestedTransactions_);
    Load(context, Parent_);
    Load(context, StartTime_);
    Load(context, StagedObjects_);
    Load(context, ImportedObjects_);
    Load(context, ExportedObjects_);
    Load(context, LockedNodes_);
    Load(context, Locks_);
    Load(context, BranchedNodes_);
    Load(context, StagedNodes_);
    Load(context, AccountResourceUsage_);
    Load(context, Acd_);
}

TYsonString TTransaction::GetDescription() const
{
    return BuildYsonStringFluently()
        .BeginMap()
            .Item("id").Value(Id_)
            .Item("start_time").Value(StartTime_)
            .DoIf(Title_.HasValue(), [&] (TFluentMap fluent) {
                fluent
                    .Item("title").Value(*Title_);
            })
            .DoIf(Parent_ != nullptr, [&] (TFluentMap fluent) {
                fluent
                    .Item("parent").Value(Parent_->GetDescription());
            })
        .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT

