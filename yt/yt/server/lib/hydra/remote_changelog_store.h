#pragma once

#include "public.h"

#include <yt/server/lib/security_server/public.h>

#include <yt/client/api/public.h>

#include <yt/client/api/client.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/client/ypath/public.h>

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

//! Creates a changelog store factory on top of DFS.
/*!
 *  If #prerequisiteTransactionId then the constructed stores are read-only.
 */
IChangelogStoreFactoryPtr CreateRemoteChangelogStoreFactory(
    TRemoteChangelogStoreConfigPtr config,
    TRemoteChangelogStoreOptionsPtr options,
    const NYPath::TYPath& path,
    NApi::IClientPtr client,
    NSecurityServer::IResourceLimitsManagerPtr resourceLimitsManager,
    NTransactionClient::TTransactionId prerequisiteTransactionId =
        NTransactionClient::NullTransactionId,
    const NApi::TJournalWriterPerformanceCounters& counters = {});

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
