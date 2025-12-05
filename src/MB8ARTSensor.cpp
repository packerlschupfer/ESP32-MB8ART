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
    // LOW_RES: Raw value 244 = 24.4°C (already in tenths - perfect!)
    // HIGH_RES: Raw value 2440 = 24.40°C (hundredths - divide by 10 to get tenths)
    // Handle negative temperatures (two's complement)
    int16_t signedValue = static_cast<int16_t>(rawData);

    if (highResolution) {
        // Convert hundredths to tenths: 2440 → 244
        return signedValue / 10;
    } else {
        // Already in tenths - direct use!
        return signedValue;
    }
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
    
    // Don't set error bits for deactivated channels
    // errorBitsToSet |= mb8art::toUnderlyingType(mb8art::getSensorErrorBit(channel));
    
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
        uint16_t rawData = (data[i * 2] << 8) | data[i * 2 + 1];
        
        MB8ART_DEBUG_ONLY(
            LOG_MB8ART_DEBUG_THROTTLED(30000, "Channel %d raw data: 0x%04X", i, rawData);
        );

        // Check for Modbus error codes:
        // 0x7530 = MB8ART sensor error code
        // 0xFFFF = Common Modbus error/no response
        // 0x0000 = Could indicate communication failure or disconnected sensor
        if (rawData == 0x7530 || rawData == 0xFFFF || rawData == 0x0000) {
            if (rawData != 0x7530) {
                // Log non-standard error codes for diagnostics (only once per 10s per channel)
                static uint32_t lastLogTime[DEFAULT_NUMBER_OF_SENSORS] = {0};
                uint32_t now = xTaskGetTickCount();
                if (now - lastLogTime[i] > pdMS_TO_TICKS(10000)) {
                    LOG_MB8ART_ERROR_NL("Channel %d: Modbus error code 0x%04X", i, rawData);
                    lastLogTime[i] = now;
                }
            }
            handleSensorError(i, statusBuffer, bufferSize, offset);
            errorBitsToSet |= (1 << i);
            sensorReadings[i].lastCommandSuccess = false;
            sensorReadings[i].isStateConfirmed = false;
            continue;
        }

        if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            float sensorValue = processChannelData(i, rawData);
            updateSensorReading(i, sensorValue, updateBitsToSet, errorBitsToSet, 
                              errorBitsToClear, statusBuffer, bufferSize, offset);
            
            // Update tracking info
            sensorReadings[i].lastCommandSuccess = true;
            sensorReadings[i].isStateConfirmed = true;
        } else {
            markChannelDeactivated(i, errorBitsToSet, statusBuffer, bufferSize, offset);
        }
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
    // Value is in tenths of degrees (Temperature_t format)
    // For display: 244 = 24.4°C

    // Check range in tenths: -2000 to 8500 (-200.0°C to 850.0°C)
    if (value >= -2000 && value <= 8500) {
        sensorReadings[channel].temperature = value;
        sensorReadings[channel].isTemperatureValid = true;
        TickType_t now = xTaskGetTickCount();
        sensorReadings[channel].lastTemperatureUpdated = now;
        sensorReadings[channel].Error = false;

        // Update bound pointers (unified mapping architecture)
<<<<<<< HEAD
        // Direct assignment - both are int16_t tenths!
=======
>>>>>>> 7b46226 (refactor: Implement unified hardware mapping architecture for sensors)
        if (sensorBindings[channel].temperaturePtr != nullptr) {
            *sensorBindings[channel].temperaturePtr = value;
        }
        if (sensorBindings[channel].validityPtr != nullptr) {
            *sensorBindings[channel].validityPtr = true;
        }

        // Update global timestamp for optimization
        lastAnyChannelUpdate = now;

        updateBitsToSet |= (1 << channel);     // Direct bit manipulation
        errorBitsToClear |= (1 << channel);    // Direct bit manipulation

        // Safe buffer append with bounds checking
        // Display in tenths: 244 → "24.4°C"
        int remaining = bufferSize - offset - 1;  // -1 for null terminator
        if (remaining > 0) {
            int written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: %d.%d°C; ", channel, value / 10, abs(value % 10));
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

        errorBitsToSet |= (1 << channel);
        
        // Safe buffer append
        int remaining = bufferSize - offset - 1;
        if (remaining > 0) {
            int written = snprintf(statusBuffer + offset, remaining,
                                 "C%d: OutOfRange(%d.%d°C); ", channel, value / 10, abs(value % 10));
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

    LOG_MB8ART_DEBUG_NL("Processing thermocouple data: Raw=0x%04X (%d), Type=%s, Temp=%d.%d°C",
                        rawData, rawData,
                        mb8art::thermocoupleTypeToString(type),
                        temperature / 10, abs(temperature % 10));

    return temperature;
}




int16_t MB8ART::processPTData(uint16_t rawData, mb8art::PTType type, mb8art::MeasurementRange range) {
    // MB8ART returns temperature data - convert to tenths of degrees
    bool isHighRes = (range == mb8art::MeasurementRange::HIGH_RES);
    int16_t temperature = convertRawToTemperature(rawData, isHighRes);

    LOG_MB8ART_DEBUG_NL("Processing PT data: Raw=0x%04X (%d), Type=%s, Temp=%d.%d°C",
                        rawData, rawData,
                        mb8art::ptTypeToString(type),
                        temperature / 10, abs(temperature % 10));

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



