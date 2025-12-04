/**
 * @file MB8ARTState.cpp
 * @brief State query and management methods
 * 
 * This file contains state query and management methods for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>
#include <cmath>

using namespace mb8art;

void MB8ART::initializeDataStructures() {
    // Clear all data structures
    memset(sensorReadings, 0, sizeof(sensorReadings));
    memset(channelConfigs, 0, sizeof(channelConfigs));
    sensorConnected = 0;  // Initialize all bits to 0
    
    // Set default values
    currentRange = mb8art::MeasurementRange::LOW_RES;
    moduleSettings = ModuleSettings();
    
    // Initialize each sensor reading with tracking
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        sensorReadings[i] = mb8art::SensorReading();  // Use default constructor
        sensorReadings[i].Error = 0;  // Set Error to false
        sensorReadings[i].lastCommandSuccess = 1;  // Initialize to true
        setSensorConnected(i, false);
        channelConfigs[i] = {
            .mode = 0,
            .subType = 0
        };
    }
}

void MB8ART::updateActiveChannelMask() {
    // Reset mask and count
    activeChannelMask = 0;
    activeChannelCount = 0;
    
    // Build active channel mask
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            EventBits_t channelBit = (1 << i);  // Direct bit, no shifting!
            activeChannelMask |= channelBit;
            activeChannelCount++;
        }
    }
    
    LOG_MB8ART_DEBUG_NL("Updated active channel mask: 0x%06X (%d active channels)", 
                       activeChannelMask, activeChannelCount);
}

bool MB8ART::waitForInitStep(EventBits_t stepBit, const char* stepName, TickType_t timeout) {
    EventBits_t bits = xEventGroupWaitBits(
        xInitEventGroup,
        stepBit,         // No shifting needed
        pdFALSE,         // Don't clear on exit
        pdTRUE,          // Wait for all bits
        timeout
    );

    bool success = (bits & stepBit) == stepBit;
    if (!success) {
        LOG_MB8ART_ERROR_NL("Timeout waiting for initialization step: %s", stepName);
    } else {
        LOG_MB8ART_DEBUG_NL("Initialization step completed: %s", stepName);
    }
    return success;
}

// Temperature sensor status methods
int16_t MB8ART::getSensorTemperature(uint8_t sensorIndex) const {
    if (sensorIndex < DEFAULT_NUMBER_OF_SENSORS) {
        int16_t temp = sensorReadings[sensorIndex].temperature;
        // Format based on measurement range
        if (currentRange == mb8art::MeasurementRange::HIGH_RES) {
            LOG_MB8ART_DEBUG_NL("getSensorTemperature(%d) = %d.%02d°C",
                               sensorIndex, temp / 100, abs(temp % 100));
        } else {
            LOG_MB8ART_DEBUG_NL("getSensorTemperature(%d) = %d.%d°C",
                               sensorIndex, temp / 10, abs(temp % 10));
        }
        return temp;
    }
    LOG_MB8ART_WARN_NL("getSensorTemperature: Invalid sensor index %d", sensorIndex);
    return 0;
}

bool MB8ART::wasSensorLastCommandSuccessful(uint8_t sensorIndex) const {
    if (sensorIndex < DEFAULT_NUMBER_OF_SENSORS) {
        return sensorReadings[sensorIndex].lastCommandSuccess;
    }
    LOG_MB8ART_WARN_NL("wasSensorLastCommandSuccessful: Invalid sensor index %d", sensorIndex);
    return false;
}

TickType_t MB8ART::getSensorLastUpdateTime(uint8_t sensorIndex) const {
    if (sensorIndex < DEFAULT_NUMBER_OF_SENSORS) {
        return sensorReadings[sensorIndex].lastTemperatureUpdated;
    }
    return 0;
}

bool MB8ART::isSensorStateConfirmed(uint8_t sensorIndex) const {
    if (sensorIndex < DEFAULT_NUMBER_OF_SENSORS) {
        return sensorReadings[sensorIndex].isStateConfirmed;
    }
    return false;
}

// Enhanced module status methods for complete observability
bool MB8ART::isModuleResponsive() const {
    // Get timing reference
    TickType_t now = xTaskGetTickCount();

    // First check: explicit offline flag
    if (statusFlags.moduleOffline) {
        return false;
    }

    // Second check: last Modbus response (handles all types of responses)
    if (lastResponseTime != 0) {
        TickType_t timeSinceModbusResponse = now - lastResponseTime;
        if (timeSinceModbusResponse < pdMS_TO_TICKS(5000)) {  // 5 second timeout
            LOG_MB8ART_DEBUG_NL("Module responsive - Modbus response %lu ms ago",
                               pdTICKS_TO_MS(timeSinceModbusResponse));
            return true;
        }
    }

    // Third check: any temperature data received
    if (lastAnyChannelUpdate != 0) {
        TickType_t timeSinceDataUpdate = now - lastAnyChannelUpdate;
        if (timeSinceDataUpdate < pdMS_TO_TICKS(5000)) {  // 5 second timeout
            LOG_MB8ART_DEBUG_NL("Module responsive - recent temperature data within %lu ms",
                               pdTICKS_TO_MS(timeSinceDataUpdate));
            return true;
        }
    }

    // If we reach here, no recent activity detected
    LOG_MB8ART_DEBUG_NL("No recent activity - performing active responsiveness check");
    
    // Last resort: active check (note: this is const, so we can't actually probe)
    // In practice, the calling code should handle probing if needed
    return false;
}

// Note: getActiveChannelCount() is now inline in MB8ART.h using the pre-computed member

uint8_t MB8ART::getConnectedChannels() const {
    return sensorConnected;
}

const char* MB8ART::getSubTypeString(mb8art::ChannelMode mode, uint8_t subType) const {
    switch (mode) {
        case mb8art::ChannelMode::THERMOCOUPLE:
            return mb8art::thermocoupleTypeToString(static_cast<mb8art::ThermocoupleType>(subType));
        case mb8art::ChannelMode::PT_INPUT:
            return mb8art::ptTypeToString(static_cast<mb8art::PTType>(subType));
        case mb8art::ChannelMode::VOLTAGE:
            return mb8art::voltageRangeToString(static_cast<mb8art::VoltageRange>(subType));
        case mb8art::ChannelMode::CURRENT:
            return mb8art::currentRangeToString(static_cast<mb8art::CurrentRange>(subType));
        default:
            return "N/A";
    }
}

bool MB8ART::isTemperatureInRange(int16_t temperature) {
    // Temperature in tenths of degrees
    static constexpr int16_t MIN_TEMPERATURE = -2000;  // -200.0°C
    static constexpr int16_t MAX_TEMPERATURE = 8000;   // 800.0°C
    return temperature >= MIN_TEMPERATURE && temperature <= MAX_TEMPERATURE;
}

void MB8ART::printModuleSettings() const {
    LOG_MB8ART_INFO_NL("=== MB8ART Module Settings ===");
    LOG_MB8ART_INFO_NL("RS485 Address: %d", moduleSettings.rs485Address);
    LOG_MB8ART_INFO_NL("Baud Rate: %s", baudRateToString(static_cast<BaudRate>(moduleSettings.baudRate)));
    LOG_MB8ART_INFO_NL("Parity: %s", parityToString(static_cast<Parity>(moduleSettings.parity)));
    LOG_MB8ART_INFO_NL("Measurement Range: %s", 
                      (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
    LOG_MB8ART_INFO_NL("Module Temperature: %.1f°C", moduleSettings.moduleTemperature);
}

BaudRate MB8ART::getStoredBaudRate() const {
    return static_cast<BaudRate>(moduleSettings.baudRate);
}

Parity MB8ART::getStoredParity() const {
    return static_cast<Parity>(moduleSettings.parity);
}

bool MB8ART::getSensorConnectionStatus(uint8_t channel) const {
    if (channel >= DEFAULT_NUMBER_OF_SENSORS) {
        return false;
    }
    return (sensorConnected & (1 << channel)) != 0;
}

const SensorReading& MB8ART::getSensorReading(uint8_t channel) const {
    static const SensorReading defaultReading{};
    
    if (channel < DEFAULT_NUMBER_OF_SENSORS) {
        return sensorReadings[channel];
    }
    
    LOG_MB8ART_WARN_NL("getSensorReading: Invalid channel %d, returning default", channel);
    return defaultReading;
}

bool MB8ART::getAllSensorReadings(mb8art::SensorReading* destination) const {
    if (destination == nullptr) {
        return false;
    }
    memcpy(destination, sensorReadings, sizeof(sensorReadings));
    return true;
}

const char* MB8ART::getTag() const {
    return tag;
}

// SimpleModbusDevice interface implementation
int16_t MB8ART::getTemperature(uint8_t channel) const {
    if (channel < DEFAULT_NUMBER_OF_SENSORS) {
        return sensorReadings[channel].temperature;  // int16_t tenths
    }
    return 0;  // Invalid temperature
}

std::vector<int16_t> MB8ART::getTemperatures() const {
    std::vector<int16_t> temps;
    temps.reserve(DEFAULT_NUMBER_OF_SENSORS);

    for (const auto& reading : sensorReadings) {
        temps.push_back(reading.temperature);  // int16_t tenths
    }

    return temps;
}

float MB8ART::getScaleFactor(size_t channel) const {
    // Scale factor for converting raw int16_t to float temperature
    // - LOW_RES: 0.1°C resolution (divide by 10)
    // - HIGH_RES: 0.01°C resolution (divide by 100)
    // Note: Prefer integer math where possible (rawTemp / 100, rawTemp % 100)
    return (currentRange == mb8art::MeasurementRange::HIGH_RES) ? 0.01f : 0.1f;
}