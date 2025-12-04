// include/config/ProjectConfig.h
// MB8ART Full Example - Project Configuration
#pragma once

#include <Arduino.h>
#include <esp_log.h>

// =============================================================================
// Project Information
// =============================================================================
#define PROJECT_NAME "MB8ART-Full-Example"
#define FIRMWARE_VERSION "1.0.0"

// =============================================================================
// Hardware Configuration (can be overridden in platformio.ini)
// =============================================================================

// Device hostname
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "esp32-mb8art-full"
#endif

// Status LED
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif

// Serial baud rate
#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 921600
#endif

// =============================================================================
// Modbus RS-485 Configuration
// =============================================================================
#ifndef MODBUS_RX_PIN
#define MODBUS_RX_PIN 36
#endif

#ifndef MODBUS_TX_PIN
#define MODBUS_TX_PIN 4
#endif

#ifndef MODBUS_BAUD_RATE
#define MODBUS_BAUD_RATE 9600
#endif

#ifndef MB8ART_ADDRESS
#define MB8ART_ADDRESS 0x03  // Boiler controller hardware
#endif

// =============================================================================
// Network Configuration
// =============================================================================
#ifndef ETH_PHY_MDC_PIN
#define ETH_PHY_MDC_PIN 23
#endif

#ifndef ETH_PHY_MDIO_PIN
#define ETH_PHY_MDIO_PIN 18
#endif

#ifndef ETH_PHY_ADDR
#define ETH_PHY_ADDR 0
#endif

#ifndef ETH_PHY_POWER_PIN
#define ETH_PHY_POWER_PIN -1
#endif

#define ETH_CONNECTION_TIMEOUT_MS 15000

// =============================================================================
// OTA Configuration
// =============================================================================
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "mb8art-update"
#endif

#ifndef OTA_PORT
#define OTA_PORT 3232
#endif

// =============================================================================
// MB8ART Configuration
// =============================================================================
#define MB8ART_NUM_CHANNELS 8

#ifndef MB8ART_HIGH_RESOLUTION
#define MB8ART_HIGH_RESOLUTION 1
#endif

#define MB8ART_REQUEST_TIMEOUT_MS 1000
#define MB8ART_RETRY_COUNT 3
#define MB8ART_INTER_REQUEST_DELAY_MS 50

// =============================================================================
// Logging Configuration
// =============================================================================

// Validate single log mode
#if !defined(LOG_MODE_RELEASE) && !defined(LOG_MODE_DEBUG_SELECTIVE) && !defined(LOG_MODE_DEBUG_FULL)
    #define LOG_MODE_RELEASE
#endif

#if (defined(LOG_MODE_RELEASE) + defined(LOG_MODE_DEBUG_SELECTIVE) + defined(LOG_MODE_DEBUG_FULL)) > 1
    #error "Only one LOG_MODE should be defined"
#endif

// =============================================================================
// Task Configuration - Stack sizes adjust based on log mode
// =============================================================================

#if defined(LOG_MODE_DEBUG_FULL)
    #define STACK_SIZE_MONITORING_TASK      5120
    #define STACK_SIZE_TEMPERATURE_TASK     4096
    #define STACK_SIZE_OTA_TASK             4096
    #define STACK_SIZE_LOOP_TASK            4096
#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    #define STACK_SIZE_MONITORING_TASK      4096
    #define STACK_SIZE_TEMPERATURE_TASK     3584
    #define STACK_SIZE_OTA_TASK             3584
    #define STACK_SIZE_LOOP_TASK            4096
#else  // LOG_MODE_RELEASE
    #define STACK_SIZE_MONITORING_TASK      3072
    #define STACK_SIZE_TEMPERATURE_TASK     3072
    #define STACK_SIZE_OTA_TASK             3072
    #define STACK_SIZE_LOOP_TASK            4096
#endif

// Task priorities (higher = more important)
#define PRIORITY_OTA_TASK           1
#define PRIORITY_MONITORING_TASK    2
#define PRIORITY_TEMPERATURE_TASK   3

// Task intervals
#if defined(LOG_MODE_DEBUG_FULL)
    #define MONITORING_INTERVAL_MS      30000   // 30s
    #define TEMPERATURE_INTERVAL_MS     2000    // 2s
#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    #define MONITORING_INTERVAL_MS      60000   // 1 min
    #define TEMPERATURE_INTERVAL_MS     5000    // 5s
#else
    #define MONITORING_INTERVAL_MS      300000  // 5 min
    #define TEMPERATURE_INTERVAL_MS     10000   // 10s
#endif

// Watchdog timeout
#define WATCHDOG_TIMEOUT_SECONDS 30

// =============================================================================
// Logging Macros - Support both custom Logger and ESP-IDF
// =============================================================================
#ifdef USE_CUSTOM_LOGGER
    #include <Logger.h>

    #define LOG_DEBUG(tag, format, ...) Logger::getInstance().log(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
    #define LOG_INFO(tag, format, ...)  Logger::getInstance().log(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
    #define LOG_WARN(tag, format, ...)  Logger::getInstance().log(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
    #define LOG_ERROR(tag, format, ...) Logger::getInstance().log(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define LOG_INFO(tag, format, ...)  ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_WARN(tag, format, ...)  ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#endif

// =============================================================================
// Error Handling
// =============================================================================

// Simple Result type for error handling
template<typename T>
class Result {
public:
    static Result<T> ok(T value) { return Result(value, true); }
    static Result<T> error() { return Result(T{}, false); }

    bool isOk() const { return success_; }
    bool isError() const { return !success_; }
    T value() const { return value_; }

private:
    Result(T value, bool success) : value_(value), success_(success) {}
    T value_;
    bool success_;
};

// Specialization for void
template<>
class Result<void> {
public:
    static Result<void> ok() { return Result(true); }
    static Result<void> error() { return Result(false); }

    bool isOk() const { return success_; }
    bool isError() const { return !success_; }

private:
    Result(bool success) : success_(success) {}
    bool success_;
};
