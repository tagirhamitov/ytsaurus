#pragma once

#include "fwd.h"

#include "http.h"

#include <util/generic/fwd.h>

namespace NYT {

///////////////////////////////////////////////////////////////////////////////

TString CreateHostNameWithPort(const TString& name, const TAuth& auth);

TString GetFullUrl(const TString& hostName, const TAuth& auth, THttpHeader& header);

TString TruncateForLogs(const TString& text, size_t maxSize);

TString GetLoggedAttributes(const THttpHeader& header, const TString& url, bool includeParameters, size_t sizeLimit);

void LogRequest(const THttpHeader& header, const TString& url, bool includeParameters, const TString& requestId, const TString& hostName);

///////////////////////////////////////////////////////////////////////////////

} // namespace NYT
