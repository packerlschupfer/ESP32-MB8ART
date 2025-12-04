// tasks/TemperatureTask.cpp
#include "tasks/TemperatureTask.h"
#include <TaskManager.h>

// External reference
extern TaskManager taskManager;

// Static member definitions
MB8ART* TemperatureTask::mb8artDevice = nullptr;
TickType_t TemperatureTask::lastSuccessfulRead = 0;
uint32_t TemperatureTask::consecutiveFailures = 0;
bool TemperatureTask::deviceWasOffline = false;

bool TemperatureTask::init(MB8ART* mb8art) {
    if (!mb8art) {
        LOG_ERROR(TASK_TAG, "Invalid MB8ART pointer");
        return false;
    }
    
    mb8artDevice = mb8art;
    lastSuccessfulRead = xTaskGetTickCount();
    consecutiveFailures = 0;
    deviceWasOffline = false;
    
    LOG_INFO(TASK_TAG, "Temperature task initialized");
    return true;
}

bool TemperatureTask::start() {
    if (isRunning()) {
        LOG_WARN(TASK_TAG, "Task already running");
        return true;
    }
    
    // Create the FreeRTOS task with watchdog config
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        true, TEMPERATURE_TASK_WATCHDOG_TIMEOUT_MS);
    
    if (!taskManager.startTask(taskFunction, TASK_NAME, STACK_SIZE, 
                              nullptr, TASK_PRIORITY, wdtConfig)) {
        LOG_ERROR(TASK_TAG, "Failed to create task");
        return false;
    }
    
    LOG_INFO(TASK_TAG, "Task started successfully with %d ms watchdog timeout", 
             TEMPERATURE_TASK_WATCHDOG_TIMEOUT_MS);
    return true;
}

void TemperatureTask::stop() {
    TaskHandle_t handle = taskManager.getTaskHandleByName(TASK_NAME);
    if (handle != nullptr) {
        (void)taskManager.stopTask(handle);
        LOG_INFO(TASK_TAG, "Task stopped");
    }
}

bool TemperatureTask::isRunning() {
    return taskManager.getTaskHandleByName(TASK_NAME) != nullptr;
}

TaskHandle_t TemperatureTask::getTaskHandle() {
    return taskManager.getTaskHandleByName(TASK_NAME);
}

TickType_t TemperatureTask::getLastReadTime() {
    return lastSuccessfulRead;
}

uint32_t TemperatureTask::getConsecutiveFailures() {
    return consecutiveFailures;
}

void TemperatureTask::taskFunction(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t currentInterval = READ_INTERVAL_MS;
    
    LOG_INFO(TASK_TAG, "Temperature acquisition task started");
    
    // Wait a bit to ensure task is registered with watchdog
    vTaskDelay(pdMS_TO_TICKS(100));
    
    while (true) {
        // Feed watchdog using TaskManager
        (void)taskManager.feedWatchdog();
        
        // Check if device is initialized first
        if (!mb8artDevice || !mb8artDevice->isInitialized()) {
            if (!deviceWasOffline) {
                LOG_ERROR(TASK_TAG, "MB8ART device not initialized - suspending temperature reads");
                deviceWasOffline = true;
            }
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(OFFLINE_RETRY_INTERVAL_MS));
            continue;
        }
        
        // Check if device is offline
        if (mb8artDevice->isModuleOffline()) {
            handleDeviceOffline();
            currentInterval = OFFLINE_RETRY_INTERVAL_MS;
        } else {
            // Device is online, read temperature data
            if (readTemperatureData()) {
                consecutiveFailures = 0;
                lastSuccessfulRead = xTaskGetTickCount();
                currentInterval = READ_INTERVAL_MS;
                
                if (deviceWasOffline) {
                    LOG_INFO(TASK_TAG, "Device back online - resuming normal operation");
                    deviceWasOffline = false;
                }
            } else {
                consecutiveFailures++;
                LOG_WARN(TASK_TAG, "Temperature read failed (failure #%d)", consecutiveFailures);
                
                // After multiple failures, increase interval
                if (consecutiveFailures > 3) {
                    currentInterval = READ_INTERVAL_MS * 2;
                }
            }
        }
        
        // Feed watchdog before delay
        (void)taskManager.feedWatchdog();
        
        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(currentInterval));
    }
}

bool TemperatureTask::readTemperatureData() {
    if (!mb8artDevice || !mb8artDevice->isInitialized()) {
        LOG_ERROR(TASK_TAG, "Device not ready");
        return false;
    }
    
    #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(TASK_TAG, "Starting temperature read cycle");
    #endif
    
    // Request temperature data from all channels
    auto result = mb8artDevice->reqTemperatures(MB8ART_NUM_CHANNELS, MB8ART_HIGH_RESOLUTION);
    
    if (!result.isOk()) {
        LOG_ERROR(TASK_TAG, "Failed to request temperatures: %d", static_cast<int>(result.error()));
        return false;
    }
    
    // Feed watchdog before waiting for data
    (void)taskManager.feedWatchdog();
    
    // Wait for data with timeout
    if (!mb8artDevice->waitForData()) {
        LOG_ERROR(TASK_TAG, "Timeout waiting for temperature data");
        return false;
    }
    
    // Feed watchdog before processing data
    (void)taskManager.feedWatchdog();
    
    // Data is already processed by waitForData()
    
    // Feed watchdog after processing
    (void)taskManager.feedWatchdog();
    
    #if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
        // Log temperature readings in debug modes
        LOG_DEBUG(TASK_TAG, "Temperature readings:");
        
        // Build a single log string to reduce time spent logging
        char tempBuffer[256];
        int offset = 0;
        
        for (int i = 0; i < MB8ART_NUM_CHANNELS; i++) {
            if (mb8artDevice->wasSensorLastCommandSuccessful(i)) {
                int16_t rawTemp = mb8artDevice->getSensorTemperature(i);
                // Get per-channel divider (PT1000+HIGH_RES=100, PT100/others=10)
                int16_t divider = mb8artDevice->getDataScaleDivider(
                    IDeviceInstance::DeviceDataType::TEMPERATURE, i);

                if (divider == 100) {
                    // HIGH_RES: hundredths (2200 = 22.00°C)
                    offset += snprintf(tempBuffer + offset, sizeof(tempBuffer) - offset,
                                     "Ch%d:%d.%02d°C ", i + 1, rawTemp / 100, abs(rawTemp % 100));
                } else {
                    // LOW_RES: tenths (220 = 22.0°C)
                    offset += snprintf(tempBuffer + offset, sizeof(tempBuffer) - offset,
                                     "Ch%d:%d.%d°C ", i + 1, rawTemp / 10, abs(rawTemp % 10));
                }
            } else {
                offset += snprintf(tempBuffer + offset, sizeof(tempBuffer) - offset,
                                 "Ch%d:-- ", i + 1);
            }

            // Feed watchdog periodically during logging
            if (i == 3) {
                (void)taskManager.feedWatchdog();
            }
        }
        
        if (offset > 0) {
            LOG_DEBUG(TASK_TAG, "  %s", tempBuffer);
        }
    #endif
    
    // Set event bits to notify other tasks
    EventGroupHandle_t eventGroup = mb8artDevice->getEventGroup();
    if (eventGroup) {
        xEventGroupSetBits(eventGroup, MB8ART::DATA_READY_BIT);
    }
    
    return true;
}

void TemperatureTask::handleDeviceOffline() {
    if (!deviceWasOffline) {
        LOG_ERROR(TASK_TAG, "MB8ART device is offline!");
        deviceWasOffline = true;
    }
    
    // Try to probe device
    if (checkDeviceRecovery()) {
        LOG_INFO(TASK_TAG, "Device probe successful - attempting recovery");
    } else {
        #if defined(LOG_MODE_DEBUG_SELECTIVE) || defined(LOG_MODE_DEBUG_FULL)
            LOG_DEBUG(TASK_TAG, "Device still offline - will retry in %d seconds", 
                     OFFLINE_RETRY_INTERVAL_MS / 1000);
        #endif
    }
}

bool TemperatureTask::checkDeviceRecovery() {
    if (!mb8artDevice) {
        return false;
    }
    
    // Try to probe the device by checking if it's initialized
    // probeDevice() is private in the library, so we use isInitialized() instead
    return mb8artDevice->isInitialized() && !mb8artDevice->isModuleOffline();
}