#pragma once

#include "public.h"

#include <core/concurrency/action_queue.h>

#include <core/concurrency/periodic_executor.h>

#include <core/ytree/public.h>
#include <core/ytree/yson_producer.h>

namespace NYT {
namespace NMonitoring {

////////////////////////////////////////////////////////////////////////////////
    
//! Provides monitoring info for registered systems in YSON format
/*!
 * \note Periodically updates info for all registered systems
 */
class TMonitoringManager
    : public TRefCounted
{
public:
    TMonitoringManager();

    //! Registers system for specified path.
    /*!
     * \param path      YPath for specified monitoring info.
     * \param producer  Monitoring info producer for the system.
     */
    void Register(const NYPath::TYPath& path, NYTree::TYsonProducer producer);

    //! Unregisters system for specified path.
    /*!
     * \param path  YPath for specified monitoring info.
     */
    void Unregister(const NYPath::TYPath& path);

    //! Returns the service providing info for all registered systems.
    /*!
     * \note The service is thread-safe.
     */
    NYTree::IYPathServicePtr GetService();

    //! Starts periodic updates.
    void Start();

    //! Stops periodic updates.
    void Stop();

private:
    class TYPathService;

    typedef yhash<Stroka, NYTree::TYsonProducer> TProducerMap;

    bool IsStarted;
    NConcurrency::TActionQueuePtr ActionQueue;
    NConcurrency::TPeriodicExecutorPtr PeriodicExecutor;

    //! Protects #MonitoringMap.
    TSpinLock SpinLock;
    TProducerMap ProducerMap;

    NYTree::INodePtr Root;

    void Update();
    void Visit(NYson::IYsonConsumer* consumer);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMonitoring
} // namespace NYT
