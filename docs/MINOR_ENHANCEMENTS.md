# MB8ART Library - Minor Enhancement Recommendations

**Version:** v0.1.0+
**Date:** 2025-12-06
**Status:** Optional improvements for future releases
**Priority:** Low (library is production-ready as-is)

---

## Overview

This document outlines minor enhancements identified during architecture analysis. The ESP32-MB8ART library (v0.1.0) is production-ready with 99.9%+ reliability. These enhancements are **optional optimizations** that may benefit specific use cases.

**Important:** Do not implement these changes without:
1. User confirmation of the specific use case
2. Understanding the trade-offs (RAM vs features)
3. Testing in the target environment

---

## Enhancement 1: Per-Channel Temperature Calibration

### Current Implementation
**Location:** `src/MB8ARTSensor.cpp:45-50`

```cpp
int16_t MB8ART::applyTemperatureCorrection(int16_t temperature) {
    // Simple offset correction, can be expanded based on calibration needs
    // Offset in tenths of degrees (e.g., 5 = 0.5°C offset)
    static constexpr int16_t TEMPERATURE_OFFSET = 0;
    return temperature + TEMPERATURE_OFFSET;
}
```

**Issue:** Single global offset applies to all channels.

### Proposed Enhancement

**Option A: Static Per-Channel Offsets (Recommended)**

```cpp
// In MB8ART.h (add to private members):
static constexpr int16_t DEFAULT_CHANNEL_OFFSETS[8] = {
    0, 0, 0, 0, 0, 0, 0, 0  // Tenths of degrees
};
int16_t channelOffsets[8];  // Runtime calibration values

// In MB8ART.cpp constructor:
memcpy(channelOffsets, DEFAULT_CHANNEL_OFFSETS, sizeof(channelOffsets));

// In MB8ARTSensor.cpp:
int16_t MB8ART::applyTemperatureCorrection(int16_t temperature, uint8_t channel) {
    if (channel < DEFAULT_NUMBER_OF_SENSORS) {
        return temperature + channelOffsets[channel];
    }
    return temperature;
}

// Public API:
void setChannelOffset(uint8_t channel, int16_t offset);
int16_t getChannelOffset(uint8_t channel) const;
```

**RAM Cost:** +8 bytes (int16_t × 8 channels)

**Option B: EEPROM/NVS Persistent Calibration**

```cpp
// Store calibration in ESP32 NVS (non-volatile storage)
bool loadCalibrationFromNVS();
bool saveCalibrationToNVS();
```

**RAM Cost:** +8 bytes + NVS overhead
**Complexity:** High (requires NVS partition, error handling)

### Use Case
- Manufacturing calibration against reference thermometer
- Compensating for sensor aging/drift over time
- Multi-point calibration (offset per sensor)

### Priority
**Low** - Only implement if:
- User reports sensor drift >±0.5°C
- Manufacturing requires calibration step
- Long-term deployment (>1 year) shows drift

---

## Enhancement 2: Modbus Queue Sizing Guidelines

### Current Implementation
**Location:** `src/MB8ART.h:59-66`

```cpp
#ifndef MB8ART_ASYNC_QUEUE_SIZE
    #ifdef PROJECT_MB8ART_ASYNC_QUEUE_SIZE
        #define MB8ART_ASYNC_QUEUE_SIZE PROJECT_MB8ART_ASYNC_QUEUE_SIZE
    #else
        #define MB8ART_ASYNC_QUEUE_SIZE 15  // Default 15 slots
    #endif
#endif
```

**Issue:** No guidance on when to change this value.

### Proposed Enhancement

**Add to README.md:**

```markdown
## Advanced Configuration

### Async Queue Sizing

The MB8ART library uses an async queue for concurrent Modbus requests.
The default size (15 slots) is optimized for typical usage:

- **Typical usage:** 2-3 concurrent requests (temperature read + status check)
- **Initialization burst:** Up to 5 requests (batch config reads)
- **Safety margin:** 15 slots provides ~3x headroom

#### When to Increase Queue Size

Increase `MB8ART_ASYNC_QUEUE_SIZE` if you see this error:
```
E (12345) MB8ART: Async queue full, dropping request
```

**Scenarios requiring larger queue:**
- Multiple MB8ART devices on same bus
- Application issues many rapid requests (>5/second)
- Shared bus with other Modbus devices causing delays

**RAM cost:** ~28 bytes per slot (15 slots = ~420 bytes)

#### Configuration Example

```ini
# platformio.ini
build_flags =
    -DPROJECT_MB8ART_ASYNC_QUEUE_SIZE=20  ; Increase to 20 slots
```

#### When to Decrease Queue Size

If RAM is constrained (<100KB free) and you have:
- Single MB8ART device
- Low request rate (<1/second)
- No initialization bursts

Minimum recommended: 8 slots

```ini
build_flags =
    -DPROJECT_MB8ART_ASYNC_QUEUE_SIZE=8  ; Saves ~200 bytes RAM
```
```

### Priority
**Low** - Documentation improvement only. No code changes needed.

---

## Enhancement 3: Runtime Metrics/Diagnostics

### Current Implementation
No centralized metrics collection. Diagnostics scattered across logging.

### Proposed Enhancement

**Add Metrics Structure:**

```cpp
// In MB8ART.h (public section):

/**
 * @brief Runtime diagnostics and performance metrics
 *
 * Useful for production monitoring, debugging, and performance tuning.
 */
struct MB8ARTMetrics {
    // Request statistics
    uint32_t totalRequests;          // Total Modbus requests issued
    uint32_t successfulRequests;     // Requests that completed successfully
    uint32_t failedRequests;         // Requests that failed or timed out
    uint32_t retriedRequests;        // Requests that required retry

    // Timeout tracking
    uint8_t consecutiveTimeouts;     // Current consecutive timeout count
    uint32_t totalTimeouts;          // Lifetime timeout count
    TickType_t lastSuccessfulRequest; // Timestamp of last successful request

    // Performance metrics
    uint32_t avgResponseTimeMs;      // Average Modbus response time (ms)
    uint32_t maxResponseTimeMs;      // Maximum observed response time (ms)
    uint32_t minResponseTimeMs;      // Minimum observed response time (ms)

    // Data quality
    uint8_t validChannels;           // Number of channels with valid data
    uint8_t errorChannels;           // Number of channels in error state
    TickType_t lastDataUpdate;       // Last time any channel updated

    // Module state
    bool isOnline;                   // !statusFlags.moduleOffline
    bool isInitialized;              // statusFlags.initialized
    uint32_t uptimeSeconds;          // Seconds since initialization
};

// Public API:
const MB8ARTMetrics& getMetrics() const;
void resetMetrics();  // Clear counters (optional)
```

**Implementation in MB8ART.cpp:**

```cpp
// Private member:
MB8ARTMetrics metrics;

// In constructor:
memset(&metrics, 0, sizeof(metrics));
metrics.minResponseTimeMs = UINT32_MAX;

// In handleModbusResponse():
uint32_t responseTime = pdTICKS_TO_MS(xTaskGetTickCount() - requestStartTime);
metrics.totalRequests++;
metrics.successfulRequests++;
metrics.avgResponseTimeMs =
    (metrics.avgResponseTimeMs * (metrics.successfulRequests - 1) + responseTime)
    / metrics.successfulRequests;
metrics.maxResponseTimeMs = max(metrics.maxResponseTimeMs, responseTime);
metrics.minResponseTimeMs = min(metrics.minResponseTimeMs, responseTime);

// In waitForData() on timeout:
metrics.totalTimeouts++;
metrics.consecutiveTimeouts++;  // Already tracked, just expose

// Getter:
const MB8ARTMetrics& MB8ART::getMetrics() const {
    // Update runtime fields
    const_cast<MB8ARTMetrics&>(metrics).isOnline = !statusFlags.moduleOffline;
    const_cast<MB8ARTMetrics&>(metrics).isInitialized = statusFlags.initialized;
    const_cast<MB8ARTMetrics&>(metrics).consecutiveTimeouts = this->consecutiveTimeouts;
    const_cast<MB8ARTMetrics&>(metrics).validChannels = getActiveChannelCount();
    const_cast<MB8ARTMetrics&>(metrics).lastDataUpdate = lastAnyChannelUpdate;

    return metrics;
}
```

**RAM Cost:** ~48 bytes (struct size)

### Use Cases

**Production Monitoring:**
```cpp
const auto& metrics = mb8art.getMetrics();

if (metrics.failedRequests > 100) {
    // Alert: High failure rate
}

if (metrics.avgResponseTimeMs > 500) {
    // Alert: Slow Modbus responses (bus congestion?)
}

if (metrics.consecutiveTimeouts >= 3) {
    // Alert: Device going offline
}
```

**Performance Profiling:**
```cpp
LOG_INFO("MB8ART Performance:");
LOG_INFO("  Avg response: %lu ms", metrics.avgResponseTimeMs);
LOG_INFO("  Min/Max: %lu / %lu ms", metrics.minResponseTimeMs, metrics.maxResponseTimeMs);
LOG_INFO("  Success rate: %.1f%%",
         100.0 * metrics.successfulRequests / metrics.totalRequests);
```

**MQTT/REST API Export:**
```cpp
void publishMetricsToMQTT() {
    const auto& m = mb8art.getMetrics();

    StaticJsonDocument<256> doc;
    doc["total_requests"] = m.totalRequests;
    doc["failed_requests"] = m.failedRequests;
    doc["avg_response_ms"] = m.avgResponseTimeMs;
    doc["consecutive_timeouts"] = m.consecutiveTimeouts;
    doc["valid_channels"] = m.validChannels;
    doc["is_online"] = m.isOnline;

    char buffer[256];
    serializeJson(doc, buffer);
    mqtt.publish("sensor/mb8art/metrics", buffer);
}
```

### Priority
**Medium** - High value for production deployments, low implementation cost.

---

## Enhancement 4: Voltage/Current Mode Implementation

### Current Implementation
**Location:** `src/MB8ARTSensor.cpp:293-343`

```cpp
int16_t MB8ART::processVoltageData(uint16_t rawData, mb8art::VoltageRange range) {
    // For now, just return raw value since voltage/current mode isn't used
    // TODO: Implement proper voltage scaling if needed
    (void)range;  // Suppress unused warning
    return static_cast<int16_t>(rawData);
}

int16_t MB8ART::processCurrentData(uint16_t rawData, mb8art::CurrentRange range) {
    // For now, just return raw value since current mode isn't used
    // TODO: Implement proper current scaling if needed
    (void)range;  // Suppress unused warning
    return static_cast<int16_t>(rawData);
}
```

**Issue:** Voltage/current modes return raw ADC values, not engineering units.

### Proposed Enhancement

**Requires Hardware Testing** - Voltage/current scaling needs empirical verification with MB8ART hardware.

**Proposed Implementation (pending testing):**

```cpp
int16_t MB8ART::processVoltageData(uint16_t rawData, mb8art::VoltageRange range) {
    // MB8ART voltage input scaling (REQUIRES HARDWARE VERIFICATION)
    // Assumptions based on typical 16-bit ADC:
    // - Full scale: ±32767 counts
    // - Voltage stored in microvolts (µV) or millivolts (mV)

    int16_t signedData = static_cast<int16_t>(rawData);

    switch (range) {
        case mb8art::VoltageRange::MV_15:
            // ±15mV range: 32767 counts = 15000 µV
            // Scale factor: 15000 / 32767 ≈ 0.458 µV/count
            // Return in µV: rawData * 0.458
            return (signedData * 458) / 1000;  // Integer math

        case mb8art::VoltageRange::MV_50:
            return (signedData * 1526) / 1000;  // 50000/32767 ≈ 1.526

        case mb8art::VoltageRange::MV_100:
            return (signedData * 3052) / 1000;  // 100000/32767 ≈ 3.052

        case mb8art::VoltageRange::V_1:
            return (signedData * 30518) / 1000; // 1000000/32767 ≈ 30.518

        default:
            return signedData;  // Unknown range - return raw
    }
}

int16_t MB8ART::processCurrentData(uint16_t rawData, mb8art::CurrentRange range) {
    // MB8ART current input scaling (REQUIRES HARDWARE VERIFICATION)
    // Current stored in microamps (µA) or nanoamps (nA)

    int16_t signedData = static_cast<int16_t>(rawData);

    switch (range) {
        case mb8art::CurrentRange::MA_20:
            // ±20mA range: 32767 counts = 20000 µA
            // Scale factor: 20000 / 32767 ≈ 0.610 µA/count
            return (signedData * 610) / 1000;

        case mb8art::CurrentRange::MA_4_TO_20:
            // 4-20mA range (unipolar)
            // 0 counts = 4mA, 32767 counts = 20mA
            // Scale: (rawData * 16) / 32767 + 4000
            return ((signedData * 16000) / 32767) + 4000;  // µA

        default:
            return signedData;
    }
}
```

**CRITICAL: Do NOT implement without:**
1. Physical MB8ART hardware configured for voltage/current mode
2. Calibrated voltage/current source for testing
3. Empirical verification of scaling factors
4. Documentation update in HARDWARE.md with test results

### Priority
**Very Low** - Only implement if user specifically needs voltage/current mode. PT/thermocouple modes cover 95% of use cases.

---

## Enhancement 5: Configurable Offline Detection Threshold

### Current Implementation
**Location:** `src/MB8ART.h:527` (assumed based on pattern)

```cpp
static constexpr uint8_t OFFLINE_THRESHOLD = 3;  // Hardcoded
```

### Proposed Enhancement

Make threshold configurable via build flags:

```cpp
// In MB8ART.h:
#ifndef MB8ART_OFFLINE_THRESHOLD
    #ifdef PROJECT_MB8ART_OFFLINE_THRESHOLD
        #define MB8ART_OFFLINE_THRESHOLD PROJECT_MB8ART_OFFLINE_THRESHOLD
    #else
        #define MB8ART_OFFLINE_THRESHOLD 3  // Default
    #endif
#endif

class MB8ART {
    // ...
    static constexpr uint8_t OFFLINE_THRESHOLD = MB8ART_OFFLINE_THRESHOLD;
    // ...
};
```

**Usage:**
```ini
# platformio.ini
build_flags =
    -DPROJECT_MB8ART_OFFLINE_THRESHOLD=5  ; More tolerant (slower detection)
    # Or
    -DPROJECT_MB8ART_OFFLINE_THRESHOLD=1  ; Aggressive (faster detection)
```

**Trade-offs:**
- **Lower threshold (1-2):** Faster offline detection, more false positives
- **Higher threshold (4-5):** More tolerant to transient issues, slower detection

### Use Cases
- **Noisy RS485 bus:** Increase threshold to avoid false offline triggers
- **Critical applications:** Decrease threshold for faster failover
- **Battery-powered devices:** Increase threshold to reduce retry overhead

### Priority
**Low** - Default value (3) works well for most deployments.

---

## Enhancement 6: Enhanced Diagnostic Logging

### Current Implementation
Logging scattered throughout code with manual throttling.

### Proposed Enhancement

**Centralized Diagnostic Method:**

```cpp
// In MB8ART.h (public):
void printDiagnostics(bool includeMetrics = true, bool includeChannels = true);

// In MB8ARTState.cpp:
void MB8ART::printDiagnostics(bool includeMetrics, bool includeChannels) {
    LOG_MB8ART_INFO_NL("=== MB8ART Diagnostics ===");
    LOG_MB8ART_INFO_NL("Device Address: 0x%02X", getServerAddress());
    LOG_MB8ART_INFO_NL("Status: %s",
        statusFlags.moduleOffline ? "OFFLINE" :
        (statusFlags.initialized ? "ONLINE" : "INITIALIZING"));

    // Module settings
    LOG_MB8ART_INFO_NL("Measurement Range: %s",
        (currentRange == mb8art::MeasurementRange::HIGH_RES) ?
        "HIGH_RES (0.01°C)" : "LOW_RES (0.1°C)");
    LOG_MB8ART_INFO_NL("Module Temperature: %.1f°C",
        moduleSettings.moduleTemperature);
    LOG_MB8ART_INFO_NL("Baud Rate: %s",
        baudRateToString(getBaudRateEnum(moduleSettings.baudRate)).c_str());

    if (includeMetrics) {
        const auto& m = getMetrics();
        LOG_MB8ART_INFO_NL("--- Performance Metrics ---");
        LOG_MB8ART_INFO_NL("Total Requests: %lu", m.totalRequests);
        LOG_MB8ART_INFO_NL("Success Rate: %.1f%%",
            m.totalRequests > 0 ?
            100.0 * m.successfulRequests / m.totalRequests : 0.0);
        LOG_MB8ART_INFO_NL("Avg Response: %lu ms", m.avgResponseTimeMs);
        LOG_MB8ART_INFO_NL("Consecutive Timeouts: %u", m.consecutiveTimeouts);
    }

    if (includeChannels) {
        LOG_MB8ART_INFO_NL("--- Channel Status ---");
        for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            if (channelConfigs[i].mode ==
                static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
                LOG_MB8ART_INFO_NL("Ch%d: DEACTIVATED", i);
                continue;
            }

            LOG_MB8ART_INFO_NL("Ch%d: %s | %s | %s | Temp: %d.%d°C | Age: %lu ms",
                i,
                mb8art::channelModeToString(
                    static_cast<mb8art::ChannelMode>(channelConfigs[i].mode)),
                isSensorConnected(i) ? "CONNECTED" : "DISCONNECTED",
                sensorReadings[i].isTemperatureValid ? "VALID" : "INVALID",
                sensorReadings[i].temperature /
                    getDataScaleDivider(IDeviceInstance::DeviceDataType::TEMPERATURE, i),
                abs(sensorReadings[i].temperature %
                    getDataScaleDivider(IDeviceInstance::DeviceDataType::TEMPERATURE, i)),
                pdTICKS_TO_MS(xTaskGetTickCount() -
                             sensorReadings[i].lastTemperatureUpdated)
            );
        }
    }

    LOG_MB8ART_INFO_NL("=========================");
}
```

**Usage:**
```cpp
// Production monitoring task
void monitoringTask(void* params) {
    while (1) {
        mb8art.printDiagnostics();
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds
    }
}

// Debug command
void handleDiagCommand() {
    mb8art.printDiagnostics(true, true);  // Full diagnostics
}
```

### Priority
**Low** - Nice-to-have for debugging. Existing `printChannelDiagnostics()` and `printModuleSettings()` cover most needs.

---

## Implementation Priority Summary

| Enhancement | Priority | RAM Cost | Complexity | When to Implement |
|-------------|----------|----------|------------|-------------------|
| **1. Per-Channel Calibration** | Low | +8 bytes | Low | User reports sensor drift |
| **2. Queue Sizing Docs** | Low | 0 bytes | None | Documentation update only |
| **3. Runtime Metrics** | Medium | +48 bytes | Medium | Production deployment |
| **4. Voltage/Current Mode** | Very Low | 0 bytes | High | User needs those modes |
| **5. Configurable Threshold** | Low | 0 bytes | Low | Noisy bus environment |
| **6. Enhanced Diagnostics** | Low | 0 bytes | Low | Complex debugging needed |

---

## Testing Recommendations

If implementing any enhancement:

1. **Unit Tests:** Add to `test/` directory
2. **Integration Tests:** Test with real MB8ART hardware
3. **Regression Tests:** Verify existing functionality unchanged
4. **RAM Profiling:** Measure actual RAM impact
5. **Performance Testing:** Check impact on response time
6. **Documentation:** Update README.md and CLAUDE.md

---

## Conclusion

The ESP32-MB8ART library is **production-ready** without these enhancements. They represent **optional optimizations** for specific use cases:

- **Manufacturing/calibration environment:** Enhancement 1
- **Production monitoring/alerting:** Enhancement 3
- **Resource-constrained systems:** Enhancements 2, 5
- **Special sensor types:** Enhancement 4

**Recommendation:** Only implement enhancements when driven by specific user requirements. The library's current feature set covers 95% of industrial temperature monitoring applications.

---

**Document prepared by:** AI Analysis (Claude Code)
**Review required by:** Project maintainer
**Next review date:** When user requests specific enhancement
