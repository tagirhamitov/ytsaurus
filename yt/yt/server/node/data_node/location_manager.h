#pragma once

#include "public.h"

#include "disk_location.h"

#include <yt/yt/library/containers/disk_manager/public.h>

#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/public.h>
#include <yt/yt/core/ytree/ypath_service.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

struct TLocationLivenessInfo
{
    TStoreLocationPtr Location;
    TString DiskId;
    ELocationState LocationState;
    bool IsDiskAlive;
};

////////////////////////////////////////////////////////////////////////////////

class TLocationManager
    : public TRefCounted
{
public:
    TLocationManager(
        IBootstrap* bootstrap,
        TChunkStorePtr chunkStore,
        IInvokerPtr controlInvoker,
        NContainers::TDiskInfoProviderPtr diskInfoProvider);

    TFuture<std::vector<TLocationLivenessInfo>> GetLocationsLiveness();

    std::vector<TString> GetConfigDiskIds();

    void SetDiskAlert(TError alert);

    TFuture<std::vector<NContainers::TDiskInfo>> GetDiskInfos();

    TFuture<std::vector<TGuid>> ResurrectChunkLocations(const THashSet<TGuid>& locationUuids);

    TFuture<std::vector<TGuid>> DisableChunkLocations(const THashSet<TGuid>& locationUuids);

    TFuture<std::vector<TGuid>> DestroyChunkLocations(const THashSet<TGuid>& locationUuids);

    TFuture<void> FailDiskByName(
        const TString& diskName,
        const TError& error);

    TFuture<void> RecoverDisk(const TString& diskId);

    void SetDiskIdsMismatched();

    NYTree::IYPathServicePtr GetOrchidService();

    std::vector<TLocationLivenessInfo> MapLocationToLivenessInfo(
        const std::vector<NContainers::TDiskInfo>& failedDisks);

    void UpdateOldDiskIds(std::vector<TString> oldDiskIds);

    std::vector<TString> GetOldDiskIds();

private:
    const NContainers::TDiskInfoProviderPtr DiskInfoProvider_;

    const TChunkStorePtr ChunkStore_;
    const IInvokerPtr ControlInvoker_;
    const NYTree::IYPathServicePtr OrchidService_;

    std::atomic<bool> DiskIdsMismatched_;
    TAtomicObject<TError> DiskFailedAlert_;
    std::vector<TString> OldDiskIds_;

    NYTree::IYPathServicePtr CreateOrchidService();

    void PopulateAlerts(std::vector<TError>* alerts);

    void BuildOrchid(NYT::NYson::IYsonConsumer* consumer);

    std::vector<TGuid> DoResurrectLocations(const THashSet<TGuid>& locationUuids);

    std::vector<TGuid> DoDisableLocations(const THashSet<TGuid>& locationUuids);

    std::vector<TGuid> DoDestroyLocations(const THashSet<TGuid>& locationUuids);

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
};

DEFINE_REFCOUNTED_TYPE(TLocationManager)

////////////////////////////////////////////////////////////////////////////////

class TLocationHealthChecker
    : public TRefCounted
{
public:
    TLocationHealthChecker(
        TChunkStorePtr chunkStore,
        TLocationManagerPtr locationManager,
        IInvokerPtr invoker,
        TRebootManagerPtr rebootManager);

    void Initialize();

    void Start();

    void OnDynamicConfigChanged(const TLocationHealthCheckerDynamicConfigPtr& newConfig);

private:
    TAtomicIntrusivePtr<TLocationHealthCheckerDynamicConfig> DynamicConfig_;

    const TChunkStorePtr ChunkStore_;
    const TLocationManagerPtr LocationManager_;
    const IInvokerPtr Invoker_;
    const TRebootManagerPtr RebootManager_;

    NConcurrency::TPeriodicExecutorPtr HealthCheckerExecutor_;

    void OnHealthCheck();

    void OnDiskHealthCheck();

    void OnLocationsHealthCheck();

    void OnDiskHealthCheckFailed(
        const TStoreLocationPtr& location,
        const TError& error);
};

DEFINE_REFCOUNTED_TYPE(TLocationHealthChecker)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
