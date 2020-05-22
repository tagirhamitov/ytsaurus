#include "authentication_identity.h"

#include <yt/core/concurrency/fls.h>

#include <yt/core/misc/format.h>

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

TAuthenticationIdentity::TAuthenticationIdentity(TString user, TString userTag)
    : User(std::move(user))
    , UserTag(std::move(userTag))
{ }

bool TAuthenticationIdentity::operator==(const TAuthenticationIdentity& other) const
{
    return
        User == other.User &&
        UserTag == other.UserTag;
}

bool TAuthenticationIdentity::operator!=(const TAuthenticationIdentity& other) const
{
    return !(*this == other);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

const TAuthenticationIdentity** GetCurrentAuthenticationIdentityPtr()
{
    static NConcurrency::TFls<const TAuthenticationIdentity*> IdentityPtr;
    return IdentityPtr.Get();
}

} // namespace

const TAuthenticationIdentity& GetCurrentAuthenticationIdentity()
{
    const auto* identity = *GetCurrentAuthenticationIdentityPtr();
    return identity ? *identity : GetRootAuthenticationIdentity();
}

const TAuthenticationIdentity& GetRootAuthenticationIdentity()
{
    static const TAuthenticationIdentity RootIdentity{
        RootUserName,
        RootUserName
    };
    return RootIdentity;
}

void SetCurrentAuthenticationIdentity(const TAuthenticationIdentity* identity)
{
    *GetCurrentAuthenticationIdentityPtr() = identity;
}

void FormatValue(TStringBuilderBase* builder, const TAuthenticationIdentity& value, TStringBuf /*format*/)
{
    builder->AppendFormat("User: %v", value.User);
    if (value.UserTag != value.User) {
        builder->AppendFormat(", UserTag: %v", value.UserTag);
    }
}

TString ToString(const TAuthenticationIdentity& value)
{
    return ToStringViaBuilder(value);
}

////////////////////////////////////////////////////////////////////////////////

TCurrentAuthenticationIdentityGuard::TCurrentAuthenticationIdentityGuard()
    : OldIdentity_(nullptr)
{ }

TCurrentAuthenticationIdentityGuard::TCurrentAuthenticationIdentityGuard(
    TCurrentAuthenticationIdentityGuard&& other)
    : OldIdentity_(other.OldIdentity_)
{
    other.OldIdentity_ = nullptr;
}

TCurrentAuthenticationIdentityGuard::TCurrentAuthenticationIdentityGuard(
    const TAuthenticationIdentity* newIdentity)
{
    OldIdentity_ = &GetCurrentAuthenticationIdentity();
    SetCurrentAuthenticationIdentity(newIdentity);
}

TCurrentAuthenticationIdentityGuard::~TCurrentAuthenticationIdentityGuard()
{
    Release();
}

TCurrentAuthenticationIdentityGuard& TCurrentAuthenticationIdentityGuard::operator=(
    TCurrentAuthenticationIdentityGuard&& other)
{
    Release();
    OldIdentity_ = other.OldIdentity_;
    other.OldIdentity_ = nullptr;
    return *this;
}

void TCurrentAuthenticationIdentityGuard::Release()
{
    if (OldIdentity_) {
        *GetCurrentAuthenticationIdentityPtr() = OldIdentity_;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc

size_t THash<NYT::NRpc::TAuthenticationIdentity>::operator()(const NYT::NRpc::TAuthenticationIdentity& value) const
{
    size_t result = 0;
    NYT::HashCombine(result, value.User);
    NYT::HashCombine(result, value.UserTag);
    return result;
}
