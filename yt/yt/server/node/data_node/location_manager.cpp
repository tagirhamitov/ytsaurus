#include "location_manager.h"

#include "private.h"

#include <yt/yt/server/node/data_node/chunk_store.h>

#include <yt/yt/server/lib/misc/reboot_manager.h>

#include <yt/yt/library/containers/disk_manager/disk_info_provider.h>

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/misc/atomic_object.h>
#include <yt/yt/core/misc/fs.h>

namespace NYT::NDataNode {

using namespace NConcurrency;
using namespace NContainers;
using namespace NFS;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

TLocationManager::TLocationManager(
    TChunkStorePtr chunkStore,
    IInvokerPtr controlInvoker,
    TDiskInfoProviderPtr diskInfoProvider)
    : DiskInfoProvider_(std::move(diskInfoProvider))
    , ChunkStore_(std::move(chunkStore))
    , ControlInvoker_(std::move(controlInvoker))
    , OrchidService_(CreateOrchidService())
{ }

void TLocationManager::BuildOrchid(IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("disk_ids_mismatched")
            .Value(DiskIdsMismatched_.load())
        .EndMap();
}

IYPathServicePtr TLocationManager::CreateOrchidService()
{
    return IYPathService::FromProducer(BIND(&TLocationManager::BuildOrchid, MakeStrong(this)))
        ->Via(ControlInvoker_);
}

IYPathServicePtr TLocationManager::GetOrchidService()
{
    return OrchidService_;
}

TFuture<void> TLocationManager::FailDiskByName(
    const TString& diskName,
    const TError& error)
{
    return DiskInfoProvider_->GetYtDiskInfos()
        .Apply(BIND([=, this, this_ = MakeStrong(this)] (const std::vector<TDiskInfo>& diskInfos) {
            for (const auto& diskInfo : diskInfos) {
                if (diskInfo.DeviceName == diskName &&
                    diskInfo.State == NContainers::EDiskState::Ok)
                {
                    // Try to fail not accessible disk.
                    return DiskInfoProvider_->FailDisk(
                        diskInfo.DiskId,
                        error.GetMessage())
                        .Apply(BIND([=] (const TError& result) {
                            if (!result.IsOK()) {
                                YT_LOG_ERROR(result,
                                    "Error marking the disk as failed (DiskName: %v)",
                                    diskInfo.DeviceName);
                            }
                        }));
                }
            }

            return VoidFuture;
        }));
}

std::vector<TLocationLivenessInfo> TLocationManager::MapLocationToLivenessInfo(
    const std::vector<TDiskInfo>& disks)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    THashMap<TString, TString> diskNameToId;
    THashSet<TString> failedDisks;

    for (const auto& disk : disks) {
        diskNameToId.emplace(std::make_pair(disk.DeviceName, disk.DiskId));
        if (disk.State == NContainers::EDiskState::Failed) {
            failedDisks.insert(disk.DeviceName);
        }
    }

    std::vector<TLocationLivenessInfo> locationLivenessInfos;
    const auto& locations = ChunkStore_->Locations();
    locationLivenessInfos.reserve(locations.size());

    for (const auto& location : locations) {
        auto it = diskNameToId.find(location->GetStaticConfig()->DeviceName);

        if (it == diskNameToId.end()) {
            YT_LOG_ERROR("Unknown location disk (DeviceName: %v)",
                location->GetStaticConfig()->DeviceName);
        } else {
            locationLivenessInfos.push_back(TLocationLivenessInfo{
                .Location = location,
                .DiskId = it->second,
                .LocationState = location->GetState(),
                .IsDiskAlive = !failedDisks.contains(location->GetStaticConfig()->DeviceName)
            });
        }
    }

    return locationLivenessInfos;
}

TFuture<std::vector<TLocationLivenessInfo>> TLocationManager::GetLocationsLiveness()
{
    return DiskInfoProvider_->GetYtDiskInfos()
        .Apply(BIND(&TLocationManager::MapLocationToLivenessInfo, MakeStrong(this))
        .AsyncVia(ControlInvoker_));
}

TFuture<std::vector<TDiskInfo>> TLocationManager::GetDiskInfos()
{
    return DiskInfoProvider_->GetYtDiskInfos();
}

std::vector<TString> TLocationManager::GetConfigDiskIds()
{
    return DiskInfoProvider_->GetConfigDiskIds();
}

std::vector<TGuid> TLocationManager::DoDisableLocations(const THashSet<TGuid>& locationUuids)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    std::vector<TGuid> locationsForDisable;

    for (const auto& location : ChunkStore_->Locations()) {
        if (locationUuids.contains(location->GetUuid())) {
            // Manual location disable.
            auto result = location->ScheduleDisable(TError("Manual location disabling")
                << TErrorAttribute("location_uuid", location->GetUuid())
                << TErrorAttribute("location_path", location->GetPath())
                << TErrorAttribute("location_disk", location->GetStaticConfig()->DeviceName));

            if (result) {
                locationsForDisable.push_back(location->GetUuid());
            }
        }
    }

    return locationsForDisable;
}

std::vector<TGuid> TLocationManager::DoDestroyLocations(const THashSet<TGuid>& locationUuids)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    std::vector<TGuid> locationsForDestroy;

    for (const auto& location : ChunkStore_->Locations()) {
        if (locationUuids.contains(location->GetUuid())) {
            // Manual location destroy.
            if (location->StartDestroy()) {
                locationsForDestroy.push_back(location->GetUuid());
            }
        }
    }

    return locationsForDestroy;
}

std::vector<TGuid> TLocationManager::DoResurrectLocations(const THashSet<TGuid>& locationUuids)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    std::vector<TGuid> locationsForResurrect;

    for (const auto& location : ChunkStore_->Locations()) {
        if (locationUuids.contains(location->GetUuid())) {
            // Manual location resurrect.

            if (location->Resurrect()) {
                locationsForResurrect.push_back(location->GetUuid());
            }
        }
    }

    return locationsForResurrect;
}

TFuture<std::vector<TGuid>> TLocationManager::DestroyChunkLocations(const THashSet<TGuid>& locationUuids)
{
    return BIND(&TLocationManager::DoDestroyLocations, MakeStrong(this))
        .AsyncVia(ControlInvoker_)
        .Run(locationUuids);
}

TFuture<std::vector<TGuid>> TLocationManager::DisableChunkLocations(const THashSet<TGuid>& locationUuids)
{
    return BIND(&TLocationManager::DoDisableLocations, MakeStrong(this))
        .AsyncVia(ControlInvoker_)
        .Run(locationUuids);
}

TFuture<std::vector<TGuid>> TLocationManager::ResurrectChunkLocations(const THashSet<TGuid>& locationUuids)
{
    return BIND(&TLocationManager::DoResurrectLocations, MakeStrong(this))
        .AsyncVia(ControlInvoker_)
        .Run(locationUuids);
}

TFuture<void> TLocationManager::RecoverDisk(const TString& diskId)
{
    return DiskInfoProvider_->RecoverDisk(diskId);
}

void TLocationManager::SetDiskIdsMismatched(bool diskIdsMismatched)
{
    return DiskIdsMismatched_.store(diskIdsMismatched);
}

////////////////////////////////////////////////////////////////////////////////

TLocationHealthChecker::TLocationHealthChecker(
    TChunkStorePtr chunkStore,
    TLocationManagerPtr locationManager,
    IInvokerPtr invoker,
    TRebootManagerPtr rebootManager)
    : DynamicConfig_(New<TLocationHealthCheckerDynamicConfig>())
    , ChunkStore_(std::move(chunkStore))
    , LocationManager_(std::move(locationManager))
    , Invoker_(std::move(invoker))
    , RebootManager_(rebootManager)
    , HealthCheckerExecutor_(New<TPeriodicExecutor>(
        Invoker_,
        BIND(&TLocationHealthChecker::OnLocationsHealthCheck, MakeWeak(this)),
        DynamicConfig_.Acquire()->HealthCheckPeriod))
{ }

void TLocationHealthChecker::Initialize()
{
    VERIFY_THREAD_AFFINITY_ANY();

    for (const auto& location : ChunkStore_->Locations()) {
        location->SubscribeDiskCheckFailed(
            BIND(&TLocationHealthChecker::OnDiskHealthCheckFailed, MakeStrong(this), location));
    }
}

void TLocationHealthChecker::Start()
{
    if (DynamicConfig_.Acquire()->Enabled) {
        YT_LOG_DEBUG("Starting location health checker");
        HealthCheckerExecutor_->Start();
    }
}

void TLocationHealthChecker::OnDynamicConfigChanged(const TLocationHealthCheckerDynamicConfigPtr& newConfig)
{
    auto config = DynamicConfig_.Acquire();
    auto oldEnabled = config->Enabled;
    auto newEnabled = newConfig->Enabled;

    auto newHealthCheckPeriod = newConfig->HealthCheckPeriod;
    HealthCheckerExecutor_->SetPeriod(newHealthCheckPeriod);

    if (oldEnabled && !newEnabled) {
        YT_LOG_DEBUG("Stopping location health checker");
        YT_UNUSED_FUTURE(HealthCheckerExecutor_->Stop());
    } else if (!oldEnabled && newEnabled) {
        YT_LOG_DEBUG("Starting location health checker");
        HealthCheckerExecutor_->Start();
    }

    DynamicConfig_.Store(newConfig);
}

void TLocationHealthChecker::OnDiskHealthCheckFailed(
    const TStoreLocationPtr& location,
    const TError& error)
{
    auto config = DynamicConfig_.Acquire();

    if (config->Enabled && config->EnableManualDiskFailures) {
        YT_UNUSED_FUTURE(LocationManager_->FailDiskByName(location->GetStaticConfig()->DeviceName, error));
    }
}

void TLocationHealthChecker::OnHealthCheck()
{
    auto config = DynamicConfig_.Acquire();

    if (config->NewDiskCheckerEnabled)
    {
        OnDiskHealthCheck();
    }

    OnLocationsHealthCheck();
}

void TLocationHealthChecker::OnDiskHealthCheck()
{
    auto diskInfos = WaitFor(LocationManager_->GetDiskInfos());

    // Fast path.
    if (!diskInfos.IsOK()) {
        YT_LOG_ERROR(diskInfos, "Failed to disks");
        return;
    }

    THashSet<TString> diskIds;

    for (const auto& disk : diskInfos.Value()) {
        diskIds.insert(disk.DiskId);
    }

    auto oldDiskIds = LocationManager_->GetConfigDiskIds();

    bool matched = true;

    if (oldDiskIds.size() < diskIds.size()) {
        matched = false;
    } else if (oldDiskIds.size() == diskIds.size()) {
        for (const auto& oldId : oldDiskIds) {
            if (!diskIds.contains(oldId)) {
                matched = false;
                break;
            }
        }
    }

    LocationManager_->SetDiskIdsMismatched(!matched);
}

void TLocationHealthChecker::OnLocationsHealthCheck()
{
    auto livenessInfosOrError = WaitFor(LocationManager_->GetLocationsLiveness());

    // Fast path.
    if (!livenessInfosOrError.IsOK()) {
        YT_LOG_ERROR(livenessInfosOrError, "Failed to list location livenesses");
        return;
    }

    const auto& livenessInfos = livenessInfosOrError.Value();

    THashSet<TString> diskWithLivenessLocations;
    THashSet<TString> diskWithNotDestroyingLocations;
    THashSet<TString> diskWithDestroyingLocations;

    for (const auto& livenessInfo : livenessInfos) {
        const auto& location = livenessInfo.Location;

        if (livenessInfo.IsDiskAlive) {
            diskWithLivenessLocations.insert(livenessInfo.DiskId);
        } else {
            location->MarkLocationDiskFailed();
        }

        if (livenessInfo.LocationState == ELocationState::Destroying) {
            diskWithDestroyingLocations.insert(livenessInfo.DiskId);
        } else {
            diskWithNotDestroyingLocations.insert(livenessInfo.DiskId);
        }
    }

    for (const auto& diskId : diskWithDestroyingLocations) {
        TError error;

        if (diskWithLivenessLocations.contains(diskId)) {
            error = TError("Disk cannot be repaired, because it contains alive locations");
        } else if (diskWithNotDestroyingLocations.contains(diskId)) {
            error = TError("Disk contains not destroying locations");
        } else {
            error = WaitFor(LocationManager_->RecoverDisk(diskId));
        }

        for (const auto& livenessInfo : livenessInfos) {
            // If disk recover request is successful than mark locations as destroyed.
            if (livenessInfo.DiskId == diskId &&
                livenessInfo.LocationState == ELocationState::Destroying)
            {
                livenessInfo.Location->FinishDestroy(error.IsOK(), error);
            }
        }
    }

    for (const auto& livenessInfo : livenessInfos) {
        if (livenessInfo.IsDiskAlive &&
            livenessInfo.LocationState == ELocationState::Destroyed)
        {
            livenessInfo.Location->OnDiskRepaired();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
