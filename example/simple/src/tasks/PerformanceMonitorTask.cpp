// PerformanceMonitorTask.cpp
//
// Task to monitor and report MB8ART performance metrics
//

#include "tasks/PerformanceMonitorTask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

// Task configuration
static const char* TASK_NAME = "PerformanceMonitor";
static const char* TASK_TAG = "PERF_MON";
static const uint32_t STACK_SIZE = 4096;
static const UBaseType_t PRIORITY = 2;
static const uint32_t REPORT_INTERVAL_MS = 30000; // 30 seconds

// Performance metrics
static struct {
    uint32_t totalRequests;
    uint32_t successfulRequests;
    uint32_t cacheHits;
    uint32_t cacheMisses;
    TickType_t totalResponseTime;
    TickType_t minResponseTime;
    TickType_t maxResponseTime;
    uint32_t dataFreshnessChecks;
    TickType_t totalFreshnessCheckTime;
} metrics = {0};

// Static member initialization
TaskManager* PerformanceMonitorTask::taskManager = nullptr;
MB8ART* PerformanceMonitorTask::mb8artDevice = nullptr;
bool PerformanceMonitorTask::initialized = false;

bool PerformanceMonitorTask::init(MB8ART* device) {
    if (initialized) {
        ESP_LOGW(TASK_TAG, "Already initialized");
        return true;
    }
    
    if (!device) {
        ESP_LOGE(TASK_TAG, "Invalid device pointer");
        return false;
    }
    
    mb8artDevice = device;
    
    // Reset metrics
    memset(&metrics, 0, sizeof(metrics));
    metrics.minResponseTime = portMAX_DELAY;
    
    initialized = true;
    ESP_LOGI(TASK_TAG, "Initialized successfully");
    return true;
}

bool PerformanceMonitorTask::start() {
    if (!initialized) {
        ESP_LOGE(TASK_TAG, "Not initialized");
        return false;
    }
    
    // Use external TaskManager instance
    extern TaskManager taskManager;
    PerformanceMonitorTask::taskManager = &taskManager;
    
    // Start the task
    TaskManager::WatchdogConfig wdConfig = TaskManager::WatchdogConfig::enabled(false, REPORT_INTERVAL_MS * 2);
    
    bool success = taskManager.startTask(
        taskFunction,
        TASK_NAME,
        STACK_SIZE,
        nullptr,
        PRIORITY,
        wdConfig
    );
    
    if (success) {
        ESP_LOGI(TASK_TAG, "Task started successfully");
    } else {
        ESP_LOGE(TASK_TAG, "Failed to start task");
    }
    
    return success;
}

void PerformanceMonitorTask::taskFunction(void* pvParameters) {
    ESP_LOGI(TASK_TAG, "Performance monitoring task started");
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t lastReportTime = xTaskGetTickCount();
    
    while (true) {
        // Feed watchdog
        (void)taskManager->feedWatchdog();
        
        // Monitor a data request cycle
        monitorDataRequest();
        
        // Check if it's time to report
        if ((xTaskGetTickCount() - lastReportTime) >= pdMS_TO_TICKS(REPORT_INTERVAL_MS)) {
            generateReport();
            lastReportTime = xTaskGetTickCount();
        }
        
        // Wait for next monitoring cycle (every 5 seconds)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    }
}

void PerformanceMonitorTask::monitorDataRequest() {
    if (!mb8artDevice || !mb8artDevice->isInitialized()) {
        return;
    }
    
    // Monitor data freshness check performance
    TickType_t freshnessStart = xTaskGetTickCount();
    (void)mb8artDevice->hasRecentSensorData(5000);  // Result used only for timing
    TickType_t freshnessTime = xTaskGetTickCount() - freshnessStart;
    
    metrics.dataFreshnessChecks++;
    metrics.totalFreshnessCheckTime += freshnessTime;
    
    // Monitor connection status check (with caching)
    static TickType_t lastConnectionCheck = 0;
    TickType_t now = xTaskGetTickCount();
    bool wasCached = (now - lastConnectionCheck) < pdMS_TO_TICKS(5000);
    
    TickType_t connStart = xTaskGetTickCount();
    (void)mb8artDevice->refreshConnectionStatus();  // Result used only for timing
    TickType_t connTime = xTaskGetTickCount() - connStart;
    
    if (wasCached && connTime < pdMS_TO_TICKS(5)) {
        metrics.cacheHits++;
    } else {
        metrics.cacheMisses++;
    }
    lastConnectionCheck = now;
    
    // Monitor temperature request
    TickType_t reqStart = xTaskGetTickCount();
    auto result = mb8artDevice->reqTemperatures();
    
    if (result.isOk()) {
        metrics.totalRequests++;
        
        if (mb8artDevice->waitForData()) {
            TickType_t responseTime = xTaskGetTickCount() - reqStart;
            
            metrics.successfulRequests++;
            metrics.totalResponseTime += responseTime;
            
            if (responseTime < metrics.minResponseTime) {
                metrics.minResponseTime = responseTime;
            }
            if (responseTime > metrics.maxResponseTime) {
                metrics.maxResponseTime = responseTime;
            }
        }
    }
}

void PerformanceMonitorTask::generateReport() {
    ESP_LOGI(TASK_TAG, "");
    ESP_LOGI(TASK_TAG, "╔═══════════════════════════════════════════════╗");
    ESP_LOGI(TASK_TAG, "║        Performance Metrics Report             ║");
    ESP_LOGI(TASK_TAG, "╚═══════════════════════════════════════════════╝");
    
    // Request statistics
    ESP_LOGI(TASK_TAG, "Temperature Requests:");
    ESP_LOGI(TASK_TAG, "  Total: %lu", metrics.totalRequests);
    uint32_t successRateTenths = metrics.totalRequests > 0 ?
        (metrics.successfulRequests * 1000) / metrics.totalRequests : 0;
    ESP_LOGI(TASK_TAG, "  Successful: %lu (%lu.%lu%%)",
             metrics.successfulRequests, successRateTenths / 10, successRateTenths % 10);
    
    // Response time statistics
    if (metrics.successfulRequests > 0) {
        uint32_t avgResponseTime = metrics.totalResponseTime / metrics.successfulRequests;
        ESP_LOGI(TASK_TAG, "Response Times:");
        ESP_LOGI(TASK_TAG, "  Average: %lu ms", pdTICKS_TO_MS(avgResponseTime));
        ESP_LOGI(TASK_TAG, "  Min: %lu ms", pdTICKS_TO_MS(metrics.minResponseTime));
        ESP_LOGI(TASK_TAG, "  Max: %lu ms", pdTICKS_TO_MS(metrics.maxResponseTime));
    }
    
    // Cache statistics
    uint32_t totalCacheAccess = metrics.cacheHits + metrics.cacheMisses;
    ESP_LOGI(TASK_TAG, "Connection Status Cache:");
    ESP_LOGI(TASK_TAG, "  Hits: %lu", metrics.cacheHits);
    ESP_LOGI(TASK_TAG, "  Misses: %lu", metrics.cacheMisses);
    if (totalCacheAccess > 0) {
        uint32_t hitRateTenths = (metrics.cacheHits * 1000) / totalCacheAccess;
        ESP_LOGI(TASK_TAG, "  Hit Rate: %lu.%lu%%", hitRateTenths / 10, hitRateTenths % 10);
    }
    
    // Data freshness check performance
    if (metrics.dataFreshnessChecks > 0) {
        uint32_t avgFreshnessTime = metrics.totalFreshnessCheckTime / metrics.dataFreshnessChecks;
        uint32_t avgFreshnessMs = avgFreshnessTime * portTICK_PERIOD_MS;
        ESP_LOGI(TASK_TAG, "Data Freshness Checks:");
        ESP_LOGI(TASK_TAG, "  Total: %lu", metrics.dataFreshnessChecks);
        ESP_LOGI(TASK_TAG, "  Avg Time: %lu ticks (%lu ms)", avgFreshnessTime, avgFreshnessMs);
    }
    
    // Memory usage
    ESP_LOGI(TASK_TAG, "Memory Usage:");
    ESP_LOGI(TASK_TAG, "  Free Heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TASK_TAG, "  Min Free Heap: %d bytes", esp_get_minimum_free_heap_size());
    
    ESP_LOGI(TASK_TAG, "═══════════════════════════════════════════════");
    ESP_LOGI(TASK_TAG, "");
}