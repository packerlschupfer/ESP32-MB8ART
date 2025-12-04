/**
 * @file MB8ARTSensor.cpp
 * @brief Sensor-specific operations and mappings
 * 
 * This file contains sensor-specific operations and mappings for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>

#include <algorithm>

using namespace mb8art;

// Spinlock for thread-safe access to static log throttle array
static portMUX_TYPE logThrottleMutex = portMUX_INITIALIZER_UNLOCKED;

// MB8ART specific methods
bool MB8ART::requestTemperatures() {
    // Prevent polling if device is offline or not initialized
    if (!statusFlags.initialized || statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("requestTemperatures blocked - device %s", 
                           statusFlags.moduleOffline ? "offline" : "not initialized");
        return false;
    }
    
    return reqTemperatures(DEFAULT_NUMBER_OF_SENSORS, 
                          currentRange == mb8art::MeasurementRange::HIGH_RES).isOk();
}

int16_t MB8ART::convertRawToTemperature(uint16_t rawData, bool highResolution) {
    // MB8ART returns temperature data:
    // LOW_RES: Raw value 244 = 24.4°C (tenths)
    // HIGH_RES: Raw value 2440 = 24.40°C (hundredths)
    //
    // We preserve the raw value - application uses getCurrentRange() to interpret:
    // - LOW_RES: divide by 10 for display (244 → 24.4°C)
    // - HIGH_RES: divide by 100 for display (2440 → 24.40°C)
    //
    // Cast handles negative temperatures (two's complement)
    (void)highResolution;  // Mode preserved in currentRange member
    return static_cast<int16_t>(rawData);
}

int16_t MB8ART::applyTemperatureCorrection(int16_t temperature) {
    // Simple offset correction, can be expanded based on calibration needs
    // Offset in tenths of degrees (e.g., 5 = 0.5°C offset)
    static constexpr int16_t TEMPERATURE_OFFSET = 0;
    return temperature + TEMPERATURE_OFFSET;
}

void MB8ART::markChannelDeactivated(uint8_t channel, EventBits_t& errorBitsToSet, 
                                   char* statusBuffer, size_t bufferSize, int& offset) {
    sensorReadings[channel].isTemperatureValid = false;
    sensorReadings[channel].Error = false;  // deactivated channels are no error
    setSensorConnected(channel, false);  // deactivated channels are not connected
    
    int remaining = bufferSize - offset - 1;
    if (remaining > 0) {
        int written = snprintf(statusBuffer + offset, remaining, "C%d: OFF; ", channel);
        if (written > 0 && written < remaining) {
            offset += written;
        }
    }
}



// Data processing methods with state tracking
void MB8ART::processTemperatureData(const uint8_t* data, size_t length,
                                  EventBits_t& updateBitsToSet,
                                  EventBits_t& errorBitsToSet,
                                  EventBits_t& errorBitsToClear,
                                  char* statusBuffer,
                                  size_t bufferSize) {
    
    MB8ART_PERF_START(process_temp_data);
    
    int offset = 0;  // Track position in buffer
    
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        // Check deactivation FIRST - skip silently without error logging
        // This prevents log flooding when channels have no physical sensors attached
        if (channelConfigs[i].mode == static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            markChannelDeactivated(i, errorBitsToSet, statusBuffer, bufferSize, offset);
            continue;
        }

        uint16_t rawData = (data[i * 2] << 8) | data[i * 2 + 1];

        MB8ART_DEBUG_ONLY(
            LOG_MB8ART_DEBUG_THROTTLED(30000, "Channel %d raw data: 0x%04X", i, rawData);
        );

        // Check for Modbus error codes (only for ACTIVE channels):
        // 0x7530 = MB8ART sensor error code
        // 0xFFFF = Common Modbus error/no response
        // 0x0000 = Could indicate communication failure or disconnected sensor
        if (rawData == 0x7530 || rawData == 0xFFFF || rawData == 0x0000) {
            if (rawData != 0x7530) {
                // Log non-standard error codes for diagnostics (only once per 10s per channel)
                // Thread-safe access to static throttle array
                static uint32_t lastLogTime[DEFAULT_NUMBER_OF_SENSORS] = {0};
                uint32_t now = xTaskGetTickCount();

                taskENTER_CRITICAL(&logThrottleMutex);
                bool shouldLog = (now - lastLogTime[i] > pdMS_TO_TICKS(10000));
                if (shouldLog) {
                    lastLogTime[i] = now;
                }
                taskEXIT_CRITICAL(&logThrottleMutex);

                if (shouldLog) {
                    LOG_MB8ART_ERROR_NL("Channel %d: Modbus error code 0x%04X", i, rawData);
                }
            }
            handleSensorError(i, statusBuffer, bufferSize, offset);
            errorBitsToSet |= mb8art::SENSOR_ERROR_BITS[i];
            sensorReadings[i].lastCommandSuccess = false;
            sensorReadings[i].isStateConfirmed = false;
            continue;
        }

        // Process valid data for active channels
        int16_t sensorValue = processChannelData(i, rawData);
        updateSensorReading(i, sensorValue, updateBitsToSet, errorBitsToSet,
                          errorBitsToClear, statusBuffer, bufferSize, offset);

        // Update tracking info
        sensorReadings[i].lastCommandSuccess = true;
        sensorReadings[i].isStateConfirmed = true;
    }
    
    MB8ART_PERF_END(process_temp_data, "Temperature data processing");
}




int16_t MB8ART::processChannelData(uint8_t channel, uint16_t rawData) {
    mb8art::ChannelMode mode = static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode);

    switch (mode) {
        case mb8art::ChannelMode::PT_INPUT: {
            auto type = static_cast<mb8art::PTType>(channelConfigs[channel].subType);
            return processPTData(rawData, type, currentRange);
        }
        case mb8art::ChannelMode::THERMOCOUPLE: {
            auto type = static_cast<mb8art::ThermocoupleType>(channelConfigs[channel].subType);
            return processThermocoupleData(rawData, type);
        }
        case mb8art::ChannelMode::VOLTAGE: {
            auto type = static_cast<mb8art::VoltageRange>(channelConfigs[channel].subType);
            return processVoltageData(rawData, type);
        }
        case mb8art::ChannelMode::CURRENT: {
            auto type = static_cast<mb8art::CurrentRange>(channelConfigs[channel].subType);
            return processCurrentData(rawData, type);
        }
        default:
            return 0;
    }
}




void MB8ART::updateSensorReading(uint8_t channel, int16_t value,
                               EventBits_t& updateBitsToSet,
                               EventBits_t& errorBitsToSet,
                               EventBits_t& errorBitsToClear,
                               char* statusBuffer,
                               size_t bufferSize,
                               int& offset) {
    // Value format depends on currentRange:
    // - LOW_RES: tenths (244 = 24.4°C)
    // - HIGH_RES: hundredths (2440 = 24.40°C)

    // Validate range based on resolution mode
    bool isHighRes = (currentRange == mb8art::MeasurementRange::HIGH_RES);
    int16_t minValid = isHighRes ? -20000 : -2000;  // -200.00°C or -200.0°C
    int16_t maxValid = isHighRes ? 85000 : 8500;    // 850.00°C or 850.0°C

    if (value >= minValid && value <= maxValid) {
        // Store raw value in internal readings (preserves full resolution)
        sensorReadings[channel].temperature = value;
        sensorReadings[channel].isTemperatureValid = true;
        TickType_t now = xTaskGetTickCount();
        sensorReadings[channel].lastTemperatureUpdated = now;
        sensorReadings[channel].Error = false;

        // Update bound pointers (unified mapping architecture)
        // ALWAYS write in tenths (Temperature_t format) for API consistency
        // Convert to tenths with proper rounding for both modes:
        // - LOW_RES: value already in tenths, use as-is
        // - HIGH_RES: value in hundredths, round to nearest tenth
        int16_t valueInTenths;
        if (isHighRes) {
            // Round hundredths to tenths using symmetric rounding
            // Examples: 735 (73.5°C) → 74 tenths (7.4°C)
            //           734 (73.4°C) → 73 tenths (7.3°C)
            //          -735 (-73.5°C) → -74 tenths (-7.4°C)
            //          -734 (-73.4°C) → -73 tenths (-7.3°C)
            if (value >= 0) {
                valueInTenths = (value + 5) / 10;  // Round positive values
            } else {
                valueInTenths = (value - 5) / 10;  // Round negative values
            }
        } else {
            // LOW_RES: value already in tenths
            valueInTenths = value;
        }

        if (sensorBindings[channel].temperaturePtr != nullptr) {
            *sensorBindings[channel].temperaturePtr = valueInTenths;
        }
        if (sensorBindings[channel].validityPtr != nullptr) {
            *sensorBindings[channel].validityPtr = true;
        }

        // Update global timestamp for optimization
        lastAnyChannelUpdate = now;

        updateBitsToSet |= mb8art::SENSOR_UPDATE_BITS[channel];
        errorBitsToClear |= mb8art::SENSOR_ERROR_BITS[channel];

        // Safe buffer append with bounds checking
        // Format based on measurement range
        int remaining = bufferSize - offset - 1;  // -1 for null terminator
        if (remaining > 0) {
            int written;
            if (currentRange == mb8art::MeasurementRange::HIGH_RES) {
                // HIGH_RES: hundredths (2200 → "22.00°C")
                written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: %d.%02d°C; ", channel, value / 100, abs(value % 100));
            } else {
                // LOW_RES: tenths (220 → "22.0°C")
                written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: %d.%d°C; ", channel, value / 10, abs(value % 10));
            }
            if (written > 0 && written < remaining) {
                offset += written;
            }
        }
    } else {
        sensorReadings[channel].isTemperatureValid = false;
        sensorReadings[channel].Error = true;

        // Update bound pointers for error case
        if (sensorBindings[channel].validityPtr != nullptr) {
            *sensorBindings[channel].validityPtr = false;
        }

        errorBitsToSet |= mb8art::SENSOR_ERROR_BITS[channel];

        // Safe buffer append - format based on measurement range
        int remaining = bufferSize - offset - 1;
        if (remaining > 0) {
            int written;
            if (currentRange == mb8art::MeasurementRange::HIGH_RES) {
                written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: OutOfRange(%d.%02d°C); ", channel, value / 100, abs(value % 100));
            } else {
                written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: OutOfRange(%d.%d°C); ", channel, value / 10, abs(value % 10));
            }
            if (written > 0 && written < remaining) {
                offset += written;
            }
        }
    }
}




int16_t MB8ART::processThermocoupleData(uint16_t rawData, mb8art::ThermocoupleType type) {
    // MB8ART returns temperature based on measurement range
    bool isHighRes = (currentRange == mb8art::MeasurementRange::HIGH_RES);
    int16_t temperature = convertRawToTemperature(rawData, isHighRes);

    if (isHighRes) {
        LOG_MB8ART_DEBUG_NL("Processing thermocouple data: Raw=0x%04X (%d), Type=%s, Temp=%d.%02d°C",
                            rawData, rawData,
                            mb8art::thermocoupleTypeToString(type),
                            temperature / 100, abs(temperature % 100));
    } else {
        LOG_MB8ART_DEBUG_NL("Processing thermocouple data: Raw=0x%04X (%d), Type=%s, Temp=%d.%d°C",
                            rawData, rawData,
                            mb8art::thermocoupleTypeToString(type),
                            temperature / 10, abs(temperature % 10));
    }

    return temperature;
}




int16_t MB8ART::processPTData(uint16_t rawData, mb8art::PTType type, mb8art::MeasurementRange range) {
    // MB8ART returns temperature data based on measurement range
    bool isHighRes = (range == mb8art::MeasurementRange::HIGH_RES);
    int16_t temperature = convertRawToTemperature(rawData, isHighRes);

    if (isHighRes) {
        LOG_MB8ART_DEBUG_NL("Processing PT data: Raw=0x%04X (%d), Type=%s, Temp=%d.%02d°C",
                            rawData, rawData,
                            mb8art::ptTypeToString(type),
                            temperature / 100, abs(temperature % 100));
    } else {
        LOG_MB8ART_DEBUG_NL("Processing PT data: Raw=0x%04X (%d), Type=%s, Temp=%d.%d°C",
                            rawData, rawData,
                            mb8art::ptTypeToString(type),
                            temperature / 10, abs(temperature % 10));
    }

    return temperature;
}




int16_t MB8ART::processVoltageData(uint16_t rawData, mb8art::VoltageRange range) {
    // For now, just return raw value since voltage/current mode isn't used
    // TODO: Implement proper voltage scaling if needed
    (void)range;  // Suppress unused warning

    LOG_MB8ART_DEBUG_NL("Processing voltage data: Raw=0x%04X, Range=%s (raw value returned)",
                        rawData,
                        mb8art::voltageRangeToString(range));

    return static_cast<int16_t>(rawData);
}




void MB8ART::processChannelConfig(uint8_t channel, uint16_t rawConfig) {
    // Extract mode and subtype from rawConfig
    channelConfigs[channel].mode = (rawConfig & 0xFF00) >> 8;  // High byte
    channelConfigs[channel].subType = rawConfig & 0x00FF;       // Low byte

    // Log the channel configuration directly
    LOG_MB8ART_DEBUG_NL(
        "Channel %d configuration successfully read: Mode=%s, SubType=%s",
        channel,
        mb8art::channelModeToString(static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode)),
        static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode) == mb8art::ChannelMode::THERMOCOUPLE
            ? mb8art::thermocoupleTypeToString(static_cast<mb8art::ThermocoupleType>(channelConfigs[channel].subType))
            : (static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode) == mb8art::ChannelMode::PT_INPUT
                ? mb8art::ptTypeToString(static_cast<mb8art::PTType>(channelConfigs[channel].subType))
                : (static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode) == mb8art::ChannelMode::VOLTAGE
                    ? mb8art::voltageRangeToString(static_cast<mb8art::VoltageRange>(channelConfigs[channel].subType))
                    : (static_cast<mb8art::ChannelMode>(channelConfigs[channel].mode) == mb8art::ChannelMode::CURRENT
                        ? mb8art::currentRangeToString(static_cast<mb8art::CurrentRange>(channelConfigs[channel].subType))
                        : "N/A")))
    );
}




int16_t MB8ART::processCurrentData(uint16_t rawData, mb8art::CurrentRange range) {
    // For now, just return raw value since current mode isn't used
    // TODO: Implement proper current scaling if needed
    (void)range;  // Suppress unused warning

    LOG_MB8ART_DEBUG_NL("Processing current data: Raw=0x%04X, Range=%s (raw value returned)",
                        rawData,
                        mb8art::currentRangeToString(range));

    return static_cast<int16_t>(rawData);
}




void MB8ART::printSensorReading(const mb8art::SensorReading& reading, int sensorIndex) {
    if (channelConfigs[sensorIndex].mode == static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
        return;
    }

    const char* statusStr = reading.isTemperatureValid ? "Valid" : "Invalid";
    const char* errorStr = reading.Error ? "Error" : "OK";
    
    LOG_MB8ART_INFO_NL("Sensor %d:", sensorIndex);
    if (reading.isTemperatureValid) {
        LOG_MB8ART_INFO_NL("  Temperature: %.2f°C", reading.temperature);
        LOG_MB8ART_INFO_NL("  Last Update: %lu ticks ago", 
                          xTaskGetTickCount() - reading.lastTemperatureUpdated);
    }
    LOG_MB8ART_INFO_NL("  Status: %s", statusStr);
    LOG_MB8ART_INFO_NL("  Error State: %s", errorStr);
}




bool MB8ART::hasRecentSensorData(TickType_t timeoutMs) const {
    if (timeoutMs == 0) {
        // No timeout specified
        LOG_MB8ART_WARN_NL("hasRecentSensorData called with 0 timeout");
        return false;
    }
    
    TickType_t now = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeoutMs);
    
    // OPTIMIZATION: Check global timestamp first
    if (lastAnyChannelUpdate != 0) {
        TickType_t timeSinceAnyUpdate = now - lastAnyChannelUpdate;
        if (timeSinceAnyUpdate < timeout) {
            // We know at least one channel was updated recently
            MB8ART_DEBUG_ONLY(
                LOG_MB8ART_DEBUG_NL("Recent data found via global timestamp (age: %d ms)", 
                                   pdTICKS_TO_MS(timeSinceAnyUpdate));
            );
            return true;
        }
    }
    
    // If global check failed, we know no channels have recent data
    MB8ART_DEBUG_ONLY(
        LOG_MB8ART_DEBUG_NL("No recent sensor data found (timeout: %d ms, last update: %d ms ago)", 
                           timeoutMs, 
                           lastAnyChannelUpdate ? pdTICKS_TO_MS(now - lastAnyChannelUpdate) : -1);
    );
    return false;
}



