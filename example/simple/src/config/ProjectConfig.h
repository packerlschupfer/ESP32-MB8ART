// ProjectConfig.h
#pragma once
#include <Arduino.h>
#include <esp_log.h>

// If not defined in platformio.ini, set defaults
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "esp32-mb8art-monitor"
#endif

// Network Settings (optional - can use WiFi instead)
#ifndef USE_ETHERNET
  #ifndef USE_WIFI
    // No network by default for MB8ART example
    // #define USE_WIFI
  #endif
#endif

#ifdef USE_WIFI
// WiFi Settings
#ifndef WIFI_SSID
#define WIFI_SSID "your-wifi-ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-wifi-password"
#endif

#ifndef WIFI_CONNECTION_TIMEOUT_MS
#define WIFI_CONNECTION_TIMEOUT_MS 30000
#endif
#endif // USE_WIFI

#ifdef USE_ETHERNET
// Ethernet PHY Settings
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
#define ETH_PHY_POWER_PIN -1  // No power pin
#endif

#define ETH_CLOCK_MODE ETH_CLOCK_GPIO17_OUT

// Ethernet connection timeout
#define ETH_CONNECTION_TIMEOUT_MS 15000
#endif // USE_ETHERNET

// OTA Settings
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "mb8art-update"  // Set your OTA password here
#endif

#ifndef OTA_PORT
#define OTA_PORT 3232
#endif

// Status LED
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2  // Onboard LED on most ESP32 dev boards
#endif

// Modbus RS-485 Settings
#define MODBUS_RX_PIN 36      // Adjust for your hardware
#define MODBUS_TX_PIN 4      // Adjust for your hardware
#define MODBUS_BAUD_RATE 9600
#define MB8ART_ADDRESS 0x03   // MB8ART address (boiler controller hardware)

// MB8ART Temperature Module Settings
#define MB8ART_NUM_CHANNELS 8
// MB8ART_HIGH_RESOLUTION is defined in platformio.ini build flags
#ifndef MB8ART_HIGH_RESOLUTION
    #define MB8ART_HIGH_RESOLUTION false  // Use low resolution by default
#endif
#define DEFAULT_TEMPERATURE_UNIT "C"  // "C" or "F"

// =============================================================================
// THREE-TIER LOGGING CONFIGURATION
// =============================================================================
// Define only ONE of these modes in your build configuration or platformio.ini:
// - LOG_MODE_RELEASE         : Minimal output (errors and critical warnings only)
// - LOG_MODE_DEBUG_SELECTIVE : Strategic debug output for key areas
// - LOG_MODE_DEBUG_FULL      : Maximum verbosity for all components

// Set default if no mode is defined
#if !defined(LOG_MODE_RELEASE) && !defined(LOG_MODE_DEBUG_SELECTIVE) && !defined(LOG_MODE_DEBUG_FULL)
    #define LOG_MODE_RELEASE  // Default to release mode for safety
#endif

// Validate that only one mode is selected
#if (defined(LOG_MODE_RELEASE) + defined(LOG_MODE_DEBUG_SELECTIVE) + defined(LOG_MODE_DEBUG_FULL)) > 1
    #error "Only one LOG_MODE should be defined"
#endif

// Task Settings - Three-tier optimization based on logging mode
// Stack sizes are in bytes, not words. Each word = 4 bytes on ESP32
// Minimum recommended is 2048 bytes for simple tasks

#if defined(LOG_MODE_DEBUG_FULL)
    // DEBUG FULL MODE - Maximum logging requires more stack
    #define STACK_SIZE_OTA_TASK              4096    // OTA needs extra for debug logging
    #define STACK_SIZE_MONITORING_TASK       5120    // Extra for extensive logging
    #define STACK_SIZE_TEMPERATURE_TASK      4096    // Extra for debug output
    #define STACK_SIZE_DATA_PROCESSING_TASK  4608    // Extra for data formatting
    #define STACK_SIZE_ALARM_TASK            3584    // Extra for alarm logging

#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    // DEBUG SELECTIVE MODE - Moderate stack usage
    #define STACK_SIZE_OTA_TASK              3072    // Standard size
    #define STACK_SIZE_MONITORING_TASK       3072    // Standard size
    #define STACK_SIZE_TEMPERATURE_TASK      3072    // Standard size
    #define STACK_SIZE_DATA_PROCESSING_TASK  3584    // Slightly more for processing
    #define STACK_SIZE_ALARM_TASK            3072    // Standard size

#else  // LOG_MODE_RELEASE
    // RELEASE MODE - Optimized stack usage for production stability
    #define STACK_SIZE_OTA_TASK              3072    // Stable size
    #define STACK_SIZE_MONITORING_TASK       3072    // Minimal monitoring
    #define STACK_SIZE_TEMPERATURE_TASK      3072    // Stable size
    #define STACK_SIZE_DATA_PROCESSING_TASK  3584    // Extra for formatting
    #define STACK_SIZE_ALARM_TASK            2560    // Minimal size
#endif

// Task priorities (lower number = lower priority, 0 = idle task priority)
// Note: FreeRTOS priorities - higher number = higher priority
// Critical path: Temperature acquisition -> Data processing -> Alarms -> Monitoring
#define PRIORITY_OTA_TASK 1                // Lowest - background updates
#define PRIORITY_MONITORING_TASK 2         // Low - general system monitoring
#define PRIORITY_ALARM_TASK 3              // Medium - temperature alarms
#define PRIORITY_DATA_PROCESSING_TASK 4    // High - data processing/formatting
#define PRIORITY_TEMPERATURE_TASK 5        // Highest - temperature acquisition
// Note: esp32ModbusRTU task runs at priority 5 (set by the library)

// Task Intervals - Can also be optimized per mode
#if defined(LOG_MODE_DEBUG_FULL)
    // Debug Full - Fast but not excessive
    #define MONITORING_TASK_INTERVAL_MS        30000    // 30 seconds
    #define TEMPERATURE_TASK_INTERVAL_MS       2000     // 2 seconds
    #define DATA_PROCESSING_TASK_INTERVAL_MS   5000     // 5 seconds
    #define ALARM_TASK_INTERVAL_MS             10000    // 10 seconds
    #define OTA_TASK_INTERVAL_MS               2000     // 2 seconds

#elif defined(LOG_MODE_DEBUG_SELECTIVE)
    // Debug Selective - Balanced
    #define MONITORING_TASK_INTERVAL_MS        60000    // 1 minute
    #define TEMPERATURE_TASK_INTERVAL_MS       5000     // 5 seconds
    #define DATA_PROCESSING_TASK_INTERVAL_MS   10000    // 10 seconds
    #define ALARM_TASK_INTERVAL_MS             30000    // 30 seconds
    #define OTA_TASK_INTERVAL_MS               3000     // 3 seconds

#else  // LOG_MODE_RELEASE
    // Release - Relaxed for production
    #define MONITORING_TASK_INTERVAL_MS        300000   // 5 minutes
    #define TEMPERATURE_TASK_INTERVAL_MS       10000    // 10 seconds
    #define DATA_PROCESSING_TASK_INTERVAL_MS   30000    // 30 seconds
    #define ALARM_TASK_INTERVAL_MS             60000    // 1 minute
    #define OTA_TASK_INTERVAL_MS               5000     // 5 seconds
#endif

// Watchdog timeouts
#define WATCHDOG_TIMEOUT_SECONDS 30
#define WATCHDOG_MIN_HEAP_BYTES 10000

// Individual task watchdog timeouts (in milliseconds)
#define OTA_TASK_WATCHDOG_TIMEOUT_MS (OTA_TASK_INTERVAL_MS * 4 + 5000)
#define MONITORING_TASK_WATCHDOG_TIMEOUT_MS (MONITORING_TASK_INTERVAL_MS + 5000)
#define TEMPERATURE_TASK_WATCHDOG_TIMEOUT_MS (TEMPERATURE_TASK_INTERVAL_MS * 4 + 5000)
#define DATA_PROCESSING_TASK_WATCHDOG_TIMEOUT_MS (DATA_PROCESSING_TASK_INTERVAL_MS * 2 + 5000)
#define ALARM_TASK_WATCHDOG_TIMEOUT_MS (ALARM_TASK_INTERVAL_MS * 2 + 5000)

// Log Tags
#define LOG_TAG_MAIN "MAIN"
#define LOG_TAG_OTA "OTA"
#define LOG_TAG_NETWORK "NET"
#define LOG_TAG_MONITORING "MON"
#define LOG_TAG_TEMPERATURE "TEMP"
#define LOG_TAG_DATA_PROC "DATA"
#define LOG_TAG_ALARM "ALARM"
#define LOG_TAG_MODBUS "MODBUS"

// MB8ART specific settings - Use PROJECT_ prefix to override library defaults
#define PROJECT_MB8ART_REQUEST_TIMEOUT_MS 1000         // Timeout for MB8ART responses
#define PROJECT_MB8ART_RETRY_COUNT 3                   // Number of retries for failed requests
#define PROJECT_MB8ART_INTER_REQUEST_DELAY_MS 50       // Delay between consecutive commands

// Temperature alarm thresholds
#define TEMP_ALARM_HIGH_THRESHOLD 80.0f         // High temperature alarm (°C)
#define TEMP_ALARM_LOW_THRESHOLD -10.0f         // Low temperature alarm (°C)
#define TEMP_ALARM_HYSTERESIS 2.0f             // Hysteresis to prevent alarm oscillation

// Data logging settings
#define ENABLE_SD_CARD_LOGGING                  // Comment out to disable SD card logging
#define SD_CARD_CS_PIN 5                        // SD card chip select pin
#define LOG_FILE_NAME "/temperature_log.csv"
#define MAX_LOG_FILE_SIZE_MB 100                // Maximum log file size before rotation

// Optional: Debug mode specific buffer sizes
#if defined(LOG_MODE_DEBUG_FULL)
    #define MODBUS_LOG_BUFFER_SIZE 512
    #define TEMP_LOG_BUFFER_SIZE 512
    #define ALARM_LOG_BUFFER_SIZE 512
#else
    #define MODBUS_LOG_BUFFER_SIZE 256
    #define TEMP_LOG_BUFFER_SIZE 256
    #define ALARM_LOG_BUFFER_SIZE 256
#endif

// Logging Macros - Support both Logger and ESP-IDF logging
#ifdef NO_LOGGER
    // Use ESP-IDF logging when Logger is not available
    #define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define LOG_INFO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_WARN(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#else
    // Use custom Logger when available
    #include <Logger.h>
    extern Logger logger;
    
    #define LOG_DEBUG(tag, format, ...) logger.log(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
    #define LOG_INFO(tag, format, ...) logger.log(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
    #define LOG_WARN(tag, format, ...) logger.log(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
    #define LOG_ERROR(tag, format, ...) logger.log(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#endif

// Temperature display mode - uncomment to enable automatic channel cycling
// #define TEMPERATURE_DISPLAY_CYCLE_MODE
#ifdef TEMPERATURE_DISPLAY_CYCLE_MODE
    #define DISPLAY_CYCLE_INTERVAL_MS 3000      // Show each channel for 3 seconds
#endif

// Memory leak test - uncomment to enable memory leak testing
// #define ENABLE_MEMORY_LEAK_TEST
#ifdef ENABLE_MEMORY_LEAK_TEST
    #define MEMORY_LEAK_TEST_DELAY_MS 150000    // Run test after 2.5 minutes
#endif