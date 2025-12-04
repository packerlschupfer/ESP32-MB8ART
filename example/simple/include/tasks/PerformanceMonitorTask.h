// PerformanceMonitorTask.h
#ifndef PERFORMANCE_MONITOR_TASK_H
#define PERFORMANCE_MONITOR_TASK_H

#include "MB8ART.h"
#include <TaskManager.h>

/**
 * @brief Task to monitor and report MB8ART performance metrics
 * 
 * This task tracks:
 * - Request/response times
 * - Cache hit rates
 * - Data freshness check performance
 * - Memory usage
 */
class PerformanceMonitorTask {
public:
    /**
     * @brief Initialize the performance monitor
     * @param device MB8ART device to monitor
     * @return true if initialization successful
     */
    static bool init(MB8ART* device);
    
    /**
     * @brief Start the performance monitoring task
     * @return true if task started successfully
     */
    static bool start();
    
    /**
     * @brief Stop the performance monitoring task
     */
    static void stop();
    
    /**
     * @brief Check if task is running
     * @return true if task is active
     */
    static bool isRunning();

private:
    static void taskFunction(void* pvParameters);
    static void monitorDataRequest();
    static void generateReport();
    
    static TaskManager* taskManager;
    static MB8ART* mb8artDevice;
    static bool initialized;
};

#endif // PERFORMANCE_MONITOR_TASK_H