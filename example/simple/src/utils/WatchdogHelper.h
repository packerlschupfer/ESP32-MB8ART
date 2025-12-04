// utils/WatchdogHelper.h
#pragma once

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config/ProjectConfig.h"

/**
 * @brief RAII wrapper for task watchdog management
 * 
 * Automatically adds task to watchdog on construction and removes on destruction
 */
class WatchdogHelper {
public:
    /**
     * Constructor - adds current task to watchdog
     * @param taskName Name of the task for logging
     * @param timeoutMs Watchdog timeout in milliseconds
     */
    WatchdogHelper(const char* taskName, uint32_t timeoutMs = WATCHDOG_TIMEOUT_SECONDS * 1000) 
        : m_taskName(taskName), m_taskHandle(xTaskGetCurrentTaskHandle()), m_isAdded(false) {
        
        #ifdef CONFIG_ESP_TASK_WDT_EN
            esp_err_t result = esp_task_wdt_add(m_taskHandle);
            if (result == ESP_OK) {
                m_isAdded = true;
                #if defined(LOG_MODE_DEBUG_FULL)
                    LOG_DEBUG(LOG_TAG_MAIN, "Task '%s' added to watchdog (timeout: %dms)", 
                             m_taskName, timeoutMs);
                #endif
            } else if (result == ESP_ERR_INVALID_ARG) {
                LOG_WARN(LOG_TAG_MAIN, "Task '%s' already in watchdog", m_taskName);
            } else {
                LOG_ERROR(LOG_TAG_MAIN, "Failed to add task '%s' to watchdog: %d", 
                         m_taskName, result);
            }
        #else
            LOG_WARN(LOG_TAG_MAIN, "Task watchdog not enabled in SDK config");
        #endif
    }
    
    /**
     * Destructor - removes task from watchdog
     */
    ~WatchdogHelper() {
        #ifdef CONFIG_ESP_TASK_WDT_EN
            if (m_isAdded && m_taskHandle) {
                esp_task_wdt_delete(m_taskHandle);
                #if defined(LOG_MODE_DEBUG_FULL)
                    LOG_DEBUG(LOG_TAG_MAIN, "Task '%s' removed from watchdog", m_taskName);
                #endif
            }
        #endif
    }
    
    /**
     * Feed the watchdog
     */
    void feed() {
        #ifdef CONFIG_ESP_TASK_WDT_EN
            if (m_isAdded) {
                esp_task_wdt_reset();
            }
        #endif
    }
    
    /**
     * Check if task was successfully added to watchdog
     * @return true if added, false otherwise
     */
    bool isActive() const {
        return m_isAdded;
    }

private:
    const char* m_taskName;
    TaskHandle_t m_taskHandle;
    bool m_isAdded;
};