#pragma once

#include "public.h"

#include <yt/ytlib/hydra/version.h>

#include <yt/core/actions/callback.h>

#include <yt/core/misc/random.h>
#include <yt/core/misc/ref.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

struct TMutationRequest
{
    TMutationRequest() = default;
    TMutationRequest(const TMutationRequest& other) = default;
    TMutationRequest(TMutationRequest&& other) noexcept = default;
    TMutationRequest(
        Stroka type,
        TSharedRef data,
        TCallback<void(TMutationContext*)> action = TCallback<void(TMutationContext*)>());

    TMutationRequest& operator = (const TMutationRequest& other) = default;
    TMutationRequest& operator = (TMutationRequest&& other) = default;

    Stroka Type;
    TSharedRef Data;
    TCallback<void(TMutationContext*)> Action;
    bool AllowLeaderForwarding = false;
};

struct TMutationResponse
{
    TMutationResponse() = default;
    TMutationResponse(const TMutationResponse& other) = default;
    TMutationResponse(TMutationResponse&& other) = default;
    explicit TMutationResponse(TSharedRefArray data);

    TMutationResponse& operator = (const TMutationResponse& other) = default;
    TMutationResponse& operator = (TMutationResponse&& other) = default;

    TSharedRefArray Data;
};

////////////////////////////////////////////////////////////////////////////////

class TMutationContext
{
public:
    TMutationContext(
        TMutationContext* parent,
        const TMutationRequest& request);

    TMutationContext(
        TVersion version,
        const TMutationRequest& request,
        TInstant timestamp,
        ui64 randomSeed);

    TVersion GetVersion() const;
    const TMutationRequest& Request() const;
    TInstant GetTimestamp() const;
    TRandomGenerator& RandomGenerator();

    TMutationResponse& Response();

private:
    TMutationContext* Parent_;
    TVersion Version_;
    const TMutationRequest& Request_;
    TMutationResponse Response_;
    TInstant Timestamp_;
    TRandomGenerator RandomGenerator_;

};

TMutationContext* TryGetCurrentMutationContext();
TMutationContext* GetCurrentMutationContext();
bool HasMutationContext();
void SetCurrentMutationContext(TMutationContext* context);

////////////////////////////////////////////////////////////////////////////////

class TMutationContextGuard
    : public TNonCopyable
{
public:
    explicit TMutationContextGuard(TMutationContext* context);
    ~TMutationContextGuard();

private:
    TMutationContext* Context_;
    TMutationContext* SavedContext_;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT

