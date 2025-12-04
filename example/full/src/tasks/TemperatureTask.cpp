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
                    int16_t rawTemp = mb8art->getSensorTemperature(ch);

                    // Check for valid reading (not error value)
                    if (rawTemp != 0) {
                        // Get per-channel divider (PT1000+HIGH_RES=100, PT100/others=10)
                        int16_t divider = mb8art->getDataScaleDivider(
                            IDeviceInstance::DeviceDataType::TEMPERATURE, ch);

                        if (divider == 100) {
                            // HIGH_RES: rawTemp is in hundredths (e.g., 2232 = 22.32°C)
                            LOG_INFO(TAG, "  CH%d: %d.%02d C", ch, rawTemp / 100, abs(rawTemp % 100));
                        } else {
                            // LOW_RES: rawTemp is in tenths (e.g., 223 = 22.3°C)
                            LOG_INFO(TAG, "  CH%d: %d.%d C", ch, rawTemp / 10, abs(rawTemp % 10));
                        }
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
