#pragma once

#include "public.h"

#include <yt/core/actions/future.h>

#include <yt/core/rpc/public.h>

namespace NYT {
namespace NAuth {

////////////////////////////////////////////////////////////////////////////////

struct ICookieAuthenticator
    : public virtual TRefCounted
{
    virtual TFuture<TAuthenticationResult> Authenticate(
        const TCookieCredentials& credentials) = 0;
};

DEFINE_REFCOUNTED_TYPE(ICookieAuthenticator)

////////////////////////////////////////////////////////////////////////////////

ICookieAuthenticatorPtr CreateBlackboxCookieAuthenticator(
    TBlackboxCookieAuthenticatorConfigPtr config,
    IBlackboxServicePtr blackbox);

ICookieAuthenticatorPtr CreateCachingCookieAuthenticator(
    TAsyncExpiringCacheConfigPtr config,
    ICookieAuthenticatorPtr authenticator);

NRpc::IAuthenticatorPtr CreateCookieAuthenticatorWrapper(
    ICookieAuthenticatorPtr underlying);

////////////////////////////////////////////////////////////////////////////////

} // namespace NAuth
} // namespace NYT
