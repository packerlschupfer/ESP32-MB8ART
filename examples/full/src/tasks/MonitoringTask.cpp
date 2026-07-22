// src/tasks/MonitoringTask.cpp
// MB8ART Full Example - System Monitoring Task

#include "config/ProjectConfig.h"
#include "init/SystemInitializer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

static const char* TAG = "Monitor";

extern SystemInitializer* gSystemInitializer;

/**
 * @brief System monitoring task
 *
 * Periodically reports system health:
 * - Free heap memory
 * - Minimum free heap (high-water mark)
 * - Task stack usage
 * - Network status
 */
void MonitoringTask(void* pvParameters) {
    (void)pvParameters;

    LOG_INFO(TAG, "Monitoring task started");

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(MONITORING_INTERVAL_MS);

    while (true) {
        LOG_INFO(TAG, "");
        LOG_INFO(TAG, "=== SYSTEM STATUS ===");

        // Memory stats
        size_t freeHeap = ESP.getFreeHeap();
        size_t minFreeHeap = ESP.getMinFreeHeap();
        size_t totalHeap = ESP.getHeapSize();

        // Integer math: percentage with one decimal place (e.g., 45.3%)
        // usedHeap * 1000 / totalHeap gives tenths of percent
        uint32_t usedHeap = totalHeap - freeHeap;
        uint32_t usedPercentTenths = totalHeap > 0 ? (usedHeap * 1000) / totalHeap : 0;
        LOG_INFO(TAG, "Heap: %d / %d bytes (%d.%d%% used)",
                 freeHeap, totalHeap,
                 usedPercentTenths / 10, usedPercentTenths % 10);
        LOG_INFO(TAG, "Min free heap: %d bytes", minFreeHeap);

        // Heap fragmentation
        size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        LOG_INFO(TAG, "Largest free block: %d bytes", largestBlock);

        // Network status
        if (gSystemInitializer) {
            LOG_INFO(TAG, "Network: %s",
                     gSystemInitializer->isNetworkConnected() ? "Connected" : "Disconnected");
        }

        // Task stats
        LOG_INFO(TAG, "Tasks: %d", uxTaskGetNumberOfTasks());

        // Stack high-water marks
        UBaseType_t stackHWM = uxTaskGetStackHighWaterMark(nullptr);
        LOG_INFO(TAG, "Monitor stack HWM: %d bytes", stackHWM * sizeof(StackType_t));

        // Uptime
        uint32_t uptimeSeconds = millis() / 1000;
        uint32_t hours = uptimeSeconds / 3600;
        uint32_t minutes = (uptimeSeconds % 3600) / 60;
        uint32_t seconds = uptimeSeconds % 60;
        LOG_INFO(TAG, "Uptime: %02d:%02d:%02d", hours, minutes, seconds);

        // Low memory warning
        if (freeHeap < 20000) {
            LOG_WARN(TAG, "LOW MEMORY WARNING!");
        }

        LOG_INFO(TAG, "=====================");
        LOG_INFO(TAG, "");

        // Wait for next interval
        vTaskDelayUntil(&lastWakeTime, interval);
    }
}
