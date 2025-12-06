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

        // Request temperature data from all channels
        if (mb8art->requestTemperatures()) {
            // Wait for data with timeout
            if (mb8art->waitForData()) {
                // Get cached temperatures
                LOG_INFO(TAG, "--- Temperature Readings ---");

                for (uint8_t ch = 0; ch < MB8ART_NUM_CHANNELS; ch++) {
                    // getSensorTemperature returns int16_t in tenths of degrees
                    int16_t tempTenths = mb8art->getSensorTemperature(ch);
                    float temp = tempTenths / 10.0f;

                    // Check for valid reading (not error value)
                    if (tempTenths != 0 && temp > -200.0f && temp < 1000.0f) {
                        LOG_INFO(TAG, "  CH%d: %.1f C", ch, temp);
                    } else {
                        LOG_DEBUG(TAG, "  CH%d: Not connected", ch);
                    }
                }

                LOG_INFO(TAG, "----------------------------");
            } else {
                LOG_WARN(TAG, "Timeout waiting for temperature data");
            }
        } else {
            LOG_WARN(TAG, "Failed to request temperatures");
        }

        // Wait for next interval
        vTaskDelayUntil(&lastWakeTime, interval);
    }
}
