// tasks/TemperatureTask.h
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/ProjectConfig.h"
#include "MB8ART.h"

/**
 * @brief Temperature acquisition task for MB8ART module
 * 
 * Periodically reads temperature data from all configured channels
 * and updates the shared data structures
 */
class TemperatureTask {
public:
    /**
     * Initialize the temperature task
     * @param mb8art Pointer to MB8ART instance
     * @return true if initialization successful, false otherwise
     */
    static bool init(MB8ART* mb8art);

    /**
     * Start the temperature task
     * @return true if task started successfully, false otherwise
     */
    static bool start();

    /**
     * Stop the temperature task
     */
    static void stop();

    /**
     * Check if the task is running
     * @return true if task is running, false otherwise
     */
    static bool isRunning();

    /**
     * Get the task handle
     * @return Task handle or nullptr if not running
     */
    static TaskHandle_t getTaskHandle();

    /**
     * Get last successful read timestamp
     * @return Tick count of last successful read
     */
    static TickType_t getLastReadTime();

    /**
     * Get number of consecutive read failures
     * @return Number of failures
     */
    static uint32_t getConsecutiveFailures();

private:
    /**
     * Main task function
     * @param pvParameters Task parameters (unused)
     */
    static void taskFunction(void* pvParameters);

    /**
     * Read temperature data from all channels
     * @return true if read successful, false otherwise
     */
    static bool readTemperatureData();

    /**
     * Handle device offline condition
     */
    static void handleDeviceOffline();

    /**
     * Check if device has recovered from offline state
     * @return true if device is back online
     */
    static bool checkDeviceRecovery();

    // Task configuration
    static constexpr const char* TASK_NAME = "TemperatureTask";
    static constexpr uint32_t STACK_SIZE = STACK_SIZE_TEMPERATURE_TASK;
    static constexpr UBaseType_t TASK_PRIORITY = PRIORITY_TEMPERATURE_TASK;
    static constexpr const char* TASK_TAG = LOG_TAG_TEMPERATURE;
    static constexpr uint32_t READ_INTERVAL_MS = TEMPERATURE_TASK_INTERVAL_MS;
    static constexpr uint32_t OFFLINE_RETRY_INTERVAL_MS = 30000;  // 30 seconds when offline

    // Task state
    static MB8ART* mb8artDevice;
    static TickType_t lastSuccessfulRead;
    static uint32_t consecutiveFailures;
    static bool deviceWasOffline;
};