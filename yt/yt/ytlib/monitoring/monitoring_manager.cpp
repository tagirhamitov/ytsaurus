#include "monitoring_manager.h"
#include "private.h"

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/periodic_executor.h>

#include <yt/core/ytree/convert.h>
#include <yt/core/ytree/ephemeral_node_factory.h>
#include <yt/core/ytree/node.h>
#include <yt/core/ytree/tree_visitor.h>
#include <yt/core/ytree/ypath_detail.h>
#include <yt/core/ytree/ypath_client.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NMonitoring {

using namespace NYTree;
using namespace NYPath;
using namespace NYson;
using namespace NRpc;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = MonitoringLogger;

static const auto UpdatePeriod = TDuration::Seconds(3);
static const auto EmptyRoot = GetEphemeralNodeFactory()->CreateMap();

////////////////////////////////////////////////////////////////////////////////

class TMonitoringManager::TImpl
    : public TRefCounted
{
public:
    void Register(const TYPath& path, TYsonProducer producer)
    {
        TGuard<TAdaptiveLock> guard(SpinLock_);
        YT_VERIFY(PathToProducer_.emplace(path, producer).second);
    }

    void Unregister(const TYPath& path)
    {
        TGuard<TAdaptiveLock> guard(SpinLock_);
        YT_VERIFY(PathToProducer_.erase(path) == 1);
    }

    IYPathServicePtr GetService()
    {
        return New<TYPathService>(this);
    }

    void Start()
    {
        TGuard<TAdaptiveLock> guard(SpinLock_);

        YT_VERIFY(!Started_);

        PeriodicExecutor_ = New<TPeriodicExecutor>(
            ActionQueue_->GetInvoker(),
            BIND(&TImpl::Update, MakeWeak(this)),
            UpdatePeriod);
        PeriodicExecutor_->Start();

        Started_ = true;
    }

    void Stop()
    {
        TGuard<TAdaptiveLock> guard(SpinLock_);

        if (!Started_)
            return;

        Started_ = false;
        PeriodicExecutor_->Stop();
        Root_.Reset();
    }

private:
    class TYPathService
        : public TYPathServiceBase
    {
    public:
        explicit TYPathService(TIntrusivePtr<TImpl> owner)
            : Owner_(std::move(owner))
        { }

        virtual TResolveResult Resolve(const TYPath& path, const IServiceContextPtr& context) override
        {
            return TResolveResultThere{Owner_->GetRoot(), path};
        }

    private:
        const TIntrusivePtr<TImpl> Owner_;

    };

    bool Started_ = false;
    TActionQueuePtr ActionQueue_ = New<TActionQueue>("Monitoring");
    TPeriodicExecutorPtr PeriodicExecutor_;

    TAdaptiveLock SpinLock_;
    THashMap<TString, NYson::TYsonProducer> PathToProducer_;
    IMapNodePtr Root_;

    void Update()
    {
        YT_LOG_DEBUG("Started updating monitoring state");
        static const auto UpdateTimer = NProfiling::TRegistry{"yt/monitoring"}.Timer("/update_time");

        NProfiling::TEventTimerGuard guard(UpdateTimer);
        auto newRoot = GetEphemeralNodeFactory()->CreateMap();
        for (const auto& [path, producer] : PathToProducer_) {
            auto value = ConvertToYsonString(producer);
            SyncYPathSet(newRoot, path, value);
        }

        if (Started_) {
            TGuard<TAdaptiveLock> guard(SpinLock_);
            std::swap(Root_, newRoot);
        }
        YT_LOG_DEBUG("Finished updating monitoring state");
    }

    IMapNodePtr GetRoot()
    {
        TGuard<TAdaptiveLock> guard(SpinLock_);
        return Root_ ? Root_ : EmptyRoot;
    }
};

DEFINE_REFCOUNTED_TYPE(TMonitoringManager)

////////////////////////////////////////////////////////////////////////////////

TMonitoringManager::TMonitoringManager()
    : Impl_(New<TImpl>())
{ }

TMonitoringManager::~TMonitoringManager() = default;

void TMonitoringManager::Register(const TYPath& path, TYsonProducer producer)
{
    Impl_->Register(path, producer);
}

void TMonitoringManager::Unregister(const TYPath& path)
{
    Impl_->Unregister(path);
}

IYPathServicePtr TMonitoringManager::GetService()
{
    return Impl_->GetService();
}

void TMonitoringManager::Start()
{
    Impl_->Start();
}

void TMonitoringManager::Stop()
{
    Impl_->Stop();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NMonitoring
