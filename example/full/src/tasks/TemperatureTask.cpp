// src/tasks/TemperatureTask.cpp
// MB8ART Full Example - Temperature Acquisition Task

#include "config/ProjectConfig.h"
#include <MB8ART.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "TempTask";

/**
 * @brief Temperature acquisition task
 *
 * Periodically reads all temperature channels from MB8ART
 * and logs the values.
 */
void TemperatureTask(void* pvParameters) {
    MB8ART* mb8art = static_cast<MB8ART*>(pvParameters);

    if (!mb8art) {
        LOG_ERROR(TAG, "No MB8ART instance provided");
        vTaskDelete(nullptr);
        return;
    }

    LOG_INFO(TAG, "Temperature task started");

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(TEMPERATURE_INTERVAL_MS);

    while (true) {
        // Check if device is online
        if (mb8art->isModuleOffline()) {
            LOG_WARN(TAG, "MB8ART offline, skipping read");
            vTaskDelayUntil(&lastWakeTime, interval);
            continue;
        }

        // Read all channels asynchronously
        auto result = mb8art->readAllChannelsAsync();

        if (result.isOk()) {
            // Get cached temperatures
            LOG_INFO(TAG, "--- Temperature Readings ---");

            for (uint8_t ch = 0; ch < MB8ART_NUM_CHANNELS; ch++) {
                auto tempResult = mb8art->getChannelTemperature(ch);

                if (tempResult.isOk()) {
                    float temp = tempResult.value();

                    // Check for valid reading
                    if (temp > -200.0f && temp < 1000.0f) {
                        LOG_INFO(TAG, "  CH%d: %.2f C", ch, temp);
                    } else {
                        LOG_DEBUG(TAG, "  CH%d: Not connected", ch);
                    }
                } else {
                    LOG_DEBUG(TAG, "  CH%d: Read error", ch);
                }
            }

            LOG_INFO(TAG, "----------------------------");
        } else {
            LOG_WARN(TAG, "Failed to read temperatures");
        }

        // Wait for next interval
        vTaskDelayUntil(&lastWakeTime, interval);
    }
}
