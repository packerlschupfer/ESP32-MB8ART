// MB8ARTLoggingMacros.h
#ifndef MB8ART_LOGGING_MACROS_H
#define MB8ART_LOGGING_MACROS_H

// Define the log tag for MB8ART
#define MB8ART_LOG_TAG "MB8ART"

// =============================================================================
// Debug mode configuration
// MUST BE BEFORE log level definitions!
// =============================================================================
// MB8ART debug is now controlled explicitly by the user
// Define MB8ART_DEBUG or MB8ART_DEBUG_SELECTIVE in your build flags to enable debug logging
#if defined(LOG_MODE_DEBUG_FULL) || defined(LOG_MODE_DEBUG_SELECTIVE)
    #define MB8ART_RELEASE_MODE 0
#else
    #define MB8ART_RELEASE_MODE 1
#endif

// For backward compatibility during transition period (will be removed in v4.0.0)
#ifdef MB8ART_DEBUG_SELECTIVE
    #ifndef MB8ART_DEBUG
        #define MB8ART_DEBUG
    #endif
#endif

// =============================================================================
// Define log levels based on debug flag (now properly set above)
// =============================================================================
#ifdef MB8ART_DEBUG
    // Debug mode: Show all levels
    #define MB8ART_LOG_LEVEL_E ESP_LOG_ERROR
    #define MB8ART_LOG_LEVEL_W ESP_LOG_WARN
    #define MB8ART_LOG_LEVEL_I ESP_LOG_INFO
    #define MB8ART_LOG_LEVEL_D ESP_LOG_DEBUG
    #define MB8ART_LOG_LEVEL_V ESP_LOG_VERBOSE
#else
    // Release mode: Only Error, Warn, Info
    #define MB8ART_LOG_LEVEL_E ESP_LOG_ERROR
    #define MB8ART_LOG_LEVEL_W ESP_LOG_WARN
    #define MB8ART_LOG_LEVEL_I ESP_LOG_INFO
    #define MB8ART_LOG_LEVEL_D ESP_LOG_NONE  // Suppress
    #define MB8ART_LOG_LEVEL_V ESP_LOG_NONE  // Suppress
#endif

// Route to custom logger or ESP-IDF
#ifdef USE_CUSTOM_LOGGER
    #include <LogInterface.h>
    #define MB8ART_LOG_E(...) LOG_WRITE(MB8ART_LOG_LEVEL_E, MB8ART_LOG_TAG, __VA_ARGS__)
    #define MB8ART_LOG_W(...) LOG_WRITE(MB8ART_LOG_LEVEL_W, MB8ART_LOG_TAG, __VA_ARGS__)
    #define MB8ART_LOG_I(...) LOG_WRITE(MB8ART_LOG_LEVEL_I, MB8ART_LOG_TAG, __VA_ARGS__)
    #ifdef MB8ART_DEBUG
        #define MB8ART_LOG_D(...) LOG_WRITE(MB8ART_LOG_LEVEL_D, MB8ART_LOG_TAG, __VA_ARGS__)
        #define MB8ART_LOG_V(...) LOG_WRITE(MB8ART_LOG_LEVEL_V, MB8ART_LOG_TAG, __VA_ARGS__)
    #else
        // Compile-time suppression for custom logger
        #define MB8ART_LOG_D(...) ((void)0)
        #define MB8ART_LOG_V(...) ((void)0)
    #endif
#else
    // ESP-IDF logging with compile-time suppression
    #include <esp_log.h>
    #define MB8ART_LOG_E(...) ESP_LOGE(MB8ART_LOG_TAG, __VA_ARGS__)
    #define MB8ART_LOG_W(...) ESP_LOGW(MB8ART_LOG_TAG, __VA_ARGS__)
    #define MB8ART_LOG_I(...) ESP_LOGI(MB8ART_LOG_TAG, __VA_ARGS__)
    #ifdef MB8ART_DEBUG
        #define MB8ART_LOG_D(...) ESP_LOGD(MB8ART_LOG_TAG, __VA_ARGS__)
        #define MB8ART_LOG_V(...) ESP_LOGV(MB8ART_LOG_TAG, __VA_ARGS__)
    #else
        #define MB8ART_LOG_D(...) ((void)0)
        #define MB8ART_LOG_V(...) ((void)0)
    #endif
#endif

// Include FreeRTOS headers for timing macros
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>  // For strlen in sensor state logging

// =============================================================================
// Main logging macros (for backward compatibility)
// =============================================================================
#define LOG_MB8ART_ERROR(...) MB8ART_LOG_E(__VA_ARGS__)
#define LOG_MB8ART_ERROR_VAR(msg, var) MB8ART_LOG_E(msg, var)
#define LOG_MB8ART_ERROR_NL(...) MB8ART_LOG_E(__VA_ARGS__)
#define LOG_MB8ART_ERROR_VAR_NL(msg, var) MB8ART_LOG_E(msg, var)
#define LOG_MB8ART_ERROR_INL(...) MB8ART_LOG_E(__VA_ARGS__)
#define LOG_MB8ART_ERROR_VAR_INL(msg, var) MB8ART_LOG_E(msg, var)

#define LOG_MB8ART_WARN(...) MB8ART_LOG_W(__VA_ARGS__)
#define LOG_MB8ART_WARN_VAR(msg, var) MB8ART_LOG_W(msg, var)
#define LOG_MB8ART_WARN_NL(...) MB8ART_LOG_W(__VA_ARGS__)
#define LOG_MB8ART_WARN_VAR_NL(msg, var) MB8ART_LOG_W(msg, var)
#define LOG_MB8ART_WARN_INL(...) MB8ART_LOG_W(__VA_ARGS__)
#define LOG_MB8ART_WARN_VAR_INL(msg, var) MB8ART_LOG_W(msg, var)

#define LOG_MB8ART_INFO(...) MB8ART_LOG_I(__VA_ARGS__)
#define LOG_MB8ART_INFO_VAR(msg, var) MB8ART_LOG_I(msg, var)
#define LOG_MB8ART_INFO_NL(...) MB8ART_LOG_I(__VA_ARGS__)
#define LOG_MB8ART_INFO_VAR_NL(msg, var) MB8ART_LOG_I(msg, var)
#define LOG_MB8ART_INFO_INL(...) MB8ART_LOG_I(__VA_ARGS__)
#define LOG_MB8ART_INFO_VAR_INL(msg, var) MB8ART_LOG_I(msg, var)

#define LOG_MB8ART_DEBUG(...) MB8ART_LOG_D(__VA_ARGS__)
#define LOG_MB8ART_DEBUG_VAR(msg, var) MB8ART_LOG_D(msg, var)
#define LOG_MB8ART_DEBUG_NL(...) MB8ART_LOG_D(__VA_ARGS__)
#define LOG_MB8ART_DEBUG_VAR_NL(msg, var) MB8ART_LOG_D(msg, var)
#define LOG_MB8ART_DEBUG_INL(...) MB8ART_LOG_D(__VA_ARGS__)
#define LOG_MB8ART_DEBUG_VAR_INL(msg, var) MB8ART_LOG_D(msg, var)

#define LOG_MB8ART_VERBOSE(...) MB8ART_LOG_V(__VA_ARGS__)
#define LOG_MB8ART_VERBOSE_VAR(msg, var) MB8ART_LOG_V(msg, var)

// =============================================================================
// Feature-specific debug flags (enabled when MB8ART_DEBUG is defined)
// =============================================================================
#ifdef MB8ART_DEBUG
    #ifndef MB8ART_DEBUG_PROTOCOL
        #define MB8ART_DEBUG_PROTOCOL   // Protocol-level debugging
    #endif
    #ifndef MB8ART_DEBUG_TIMING
        #define MB8ART_DEBUG_TIMING     // Performance timing
    #endif
    #ifndef MB8ART_DEBUG_BUFFER
        #define MB8ART_DEBUG_BUFFER     // Buffer dumps
    #endif
    #ifndef MB8ART_DEBUG_FULL
        #define MB8ART_DEBUG_FULL       // Full debug mode
    #endif
#endif

// =============================================================================
// Performance timing macros
// =============================================================================
#ifdef MB8ART_DEBUG_TIMING
    #define MB8ART_PERF_START(name) TickType_t _perf_start_##name = xTaskGetTickCount()
    #define MB8ART_PERF_END(name, msg) \
        do { \
            TickType_t _perf_elapsed_##name = xTaskGetTickCount() - _perf_start_##name; \
            MB8ART_LOG_D("%s took %d ms", msg, pdTICKS_TO_MS(_perf_elapsed_##name)); \
        } while(0)
    #define MB8ART_PERF_END_WARN(name, msg, threshold_ms) \
        do { \
            TickType_t _perf_elapsed_##name = xTaskGetTickCount() - _perf_start_##name; \
            if (pdTICKS_TO_MS(_perf_elapsed_##name) > threshold_ms) { \
                MB8ART_LOG_W("%s took %d ms (threshold: %d ms)", \
                           msg, pdTICKS_TO_MS(_perf_elapsed_##name), threshold_ms); \
            } else { \
                MB8ART_LOG_D("%s took %d ms", msg, pdTICKS_TO_MS(_perf_elapsed_##name)); \
            } \
        } while(0)
#else
    #define MB8ART_PERF_START(name) ((void)0)
    #define MB8ART_PERF_END(name, msg) ((void)0)
    #define MB8ART_PERF_END_WARN(name, msg, threshold_ms) ((void)0)
#endif

// =============================================================================
// Stack monitoring macros
// =============================================================================
#ifdef MB8ART_DEBUG_FULL
    #define MB8ART_STACK_CHECK_START() UBaseType_t _stack_start = uxTaskGetStackHighWaterMark(nullptr)
    #define MB8ART_STACK_CHECK_END(msg) \
        do { \
            UBaseType_t _stack_end = uxTaskGetStackHighWaterMark(nullptr); \
            UBaseType_t _stack_used = (_stack_start - _stack_end) * 4; \
            MB8ART_LOG_D("%s - Stack: start=%d, end=%d, used=%d bytes", \
                        msg, _stack_start * 4, _stack_end * 4, _stack_used); \
        } while(0)
    #define MB8ART_STACK_CHECK_POINT(msg) \
        do { \
            UBaseType_t _stack_free = uxTaskGetStackHighWaterMark(nullptr) * 4; \
            MB8ART_LOG_D("%s - Stack free: %d bytes", msg, _stack_free); \
        } while(0)
#else
    #define MB8ART_STACK_CHECK_START() ((void)0)
    #define MB8ART_STACK_CHECK_END(msg) ((void)0)
    #define MB8ART_STACK_CHECK_POINT(msg) ((void)0)
#endif

// =============================================================================
// Critical operations logging
// =============================================================================
#ifdef MB8ART_DEBUG_PROTOCOL
    #define MB8ART_LOG_CRITICAL_ENTRY(section) MB8ART_LOG_D(">>> Entering: %s", section)
    #define MB8ART_LOG_CRITICAL_EXIT(section) MB8ART_LOG_D("<<< Exiting: %s", section)
#else
    #define MB8ART_LOG_CRITICAL_ENTRY(section) ((void)0)
    #define MB8ART_LOG_CRITICAL_EXIT(section) ((void)0)
#endif

// =============================================================================
// Modbus packet logging
// =============================================================================
#ifdef MB8ART_DEBUG_BUFFER
    #define MB8ART_LOG_MODBUS_PACKET(prefix, data, len) \
        do { \
            if (data && len > 0) { \
                char hex_str[64]; \
                int pos = 0; \
                for (size_t i = 0; i < len && i < 20; i++) { \
                    pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X ", data[i]); \
                } \
                if (len > 20) { \
                    pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "..."); \
                } \
                MB8ART_LOG_D("%s [%d bytes]: %s", prefix, len, hex_str); \
            } \
        } while(0)
    #define MB8ART_DUMP_BUFFER(msg, buf, len) \
        do { \
            MB8ART_LOG_D("%s (%d bytes):", msg, len); \
            for (int i = 0; i < len && i < 32; i++) { \
                MB8ART_LOG_D("  [%02d] = 0x%02X", i, buf[i]); \
            } \
            if (len > 32) { \
                MB8ART_LOG_D("  ... (%d more bytes)", len - 32); \
            } \
        } while(0)
#else
    #define MB8ART_LOG_MODBUS_PACKET(prefix, data, len) ((void)0)
    #define MB8ART_DUMP_BUFFER(msg, buf, len) ((void)0)
#endif

// =============================================================================
// Event bits logging
// =============================================================================
#ifdef MB8ART_DEBUG_PROTOCOL
    #define MB8ART_LOG_EVENT_BITS(event_group, prefix) \
        do { \
            if (event_group) { \
                EventBits_t bits = xEventGroupGetBits(event_group); \
                MB8ART_LOG_D("%s: 0x%08X", prefix, bits); \
            } \
        } while(0)
#else
    #define MB8ART_LOG_EVENT_BITS(event_group, prefix) ((void)0)
#endif

// =============================================================================
// Initialization progress logging
// =============================================================================
#ifdef MB8ART_DEBUG
    #define MB8ART_LOG_INIT_STEP(step) MB8ART_LOG_I("Init step: %s", step)
    #define MB8ART_LOG_INIT_COMPLETE() MB8ART_LOG_I("*** MB8ART Initialization Complete ***")
#else
    #define MB8ART_LOG_INIT_STEP(step) ((void)0)
    #define MB8ART_LOG_INIT_COMPLETE() MB8ART_LOG_I("MB8ART Ready")
#endif

// =============================================================================
// Sensor state logging
// =============================================================================
// Sensor state change logging - always visible for operational awareness
#define MB8ART_LOG_SENSOR_CHANGE(sensor_num, old_temp, new_temp) \
    MB8ART_LOG_I("Sensor %d: %.1f°C -> %.1f°C", sensor_num, old_temp, new_temp)

// Summary logging for sensor states
#ifdef MB8ART_DEBUG
    #define MB8ART_LOG_ALL_SENSOR_STATES(readings, num_sensors) \
        do { \
            char status[256] = "Sensor States: "; \
            int pos = strlen(status); \
            for (int i = 0; i < num_sensors && pos < 200; i++) { \
                if (readings[i].isTemperatureValid) { \
                    pos += snprintf(status + pos, sizeof(status) - pos, \
                                  "S%d:%.1f°C ", i + 1, readings[i].temperature); \
                } else { \
                    pos += snprintf(status + pos, sizeof(status) - pos, \
                                  "S%d:ERR ", i + 1); \
                } \
            } \
            MB8ART_LOG_I("%s", status); \
        } while(0)
#else
    #define MB8ART_LOG_ALL_SENSOR_STATES(readings, num_sensors) ((void)0)
#endif

// =============================================================================
// Throttled logging macros
// =============================================================================
#ifdef MB8ART_DEBUG
    #define MB8ART_LOG_THROTTLED(interval_ms, format, ...) \
        do { \
            static TickType_t _last_log = 0; \
            TickType_t _now = xTaskGetTickCount(); \
            if (_now - _last_log >= pdMS_TO_TICKS(interval_ms)) { \
                MB8ART_LOG_D(format, ##__VA_ARGS__); \
                _last_log = _now; \
            } \
        } while(0)
    #define LOG_MB8ART_INFO_THROTTLED(interval_ms, format, ...) \
        do { \
            static TickType_t _last_log = 0; \
            TickType_t _now = xTaskGetTickCount(); \
            if (_now - _last_log >= pdMS_TO_TICKS(interval_ms)) { \
                MB8ART_LOG_I(format, ##__VA_ARGS__); \
                _last_log = _now; \
            } \
        } while(0)
    #define LOG_MB8ART_DEBUG_THROTTLED(interval_ms, format, ...) \
        MB8ART_LOG_THROTTLED(interval_ms, format, ##__VA_ARGS__)
#else
    #define MB8ART_LOG_THROTTLED(interval_ms, format, ...) ((void)0)
    #define LOG_MB8ART_INFO_THROTTLED(interval_ms, format, ...) ((void)0)
    #define LOG_MB8ART_DEBUG_THROTTLED(interval_ms, format, ...) ((void)0)
#endif

// =============================================================================
// Utility macros
// =============================================================================
// Note: Auto-enable logic has been moved to the top of the file
// to ensure MB8ART_DEBUG is defined before log levels are set

// Conditional code execution based on mode
#ifdef MB8ART_RELEASE_MODE
    #define MB8ART_DEBUG_ONLY(code) ((void)0)
    #define MB8ART_RELEASE_ONLY(code) do { code } while(0)
    #define MB8ART_DEBUG_BLOCK if(0)
#else
    #define MB8ART_DEBUG_ONLY(code) do { code } while(0)
    #define MB8ART_RELEASE_ONLY(code) ((void)0)
    #define MB8ART_DEBUG_BLOCK if(1)
#endif

#endif // MB8ART_LOGGING_MACROS_H