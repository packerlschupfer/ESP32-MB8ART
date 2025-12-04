/**
 * @file MB8ARTConfig.cpp
 * @brief Configuration and settings management
 * 
 * This file contains configuration and settings management for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>

using namespace mb8art;

bool MB8ART::batchReadAllConfig() {
    MB8ART_LOG_INIT_STEP("Attempting batch configuration read...");
    
    // First batch: Read all channel configurations (128-135 = 8 registers) - CRITICAL
    LOG_MB8ART_DEBUG_NL("Reading channel configurations first (critical data)");
    auto channelResult = readHoldingRegisters(CHANNEL_CONFIG_REGISTER_START, DEFAULT_NUMBER_OF_SENSORS);
    if (!channelResult.isOk() || channelResult.value().size() < DEFAULT_NUMBER_OF_SENSORS) {
        LOG_MB8ART_ERROR_NL("Failed to read channel configs batch (error: %d)", 
                           static_cast<int>(channelResult.error()));
        return false;
    }
    
    // Process each channel configuration
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        processChannelConfig(i, channelResult.value()[i]);
    }
    
    // Update the pre-computed active channel mask
    updateActiveChannelMask();
    
    setInitializationBit(InitBits::CHANNEL_CONFIG);
    LOG_MB8ART_DEBUG_NL("Channel configs batch read successful");

    // Second batch: Read module settings and measurement range (70-76 = 7 registers) - CRITICAL
    constexpr uint16_t MODULE_BATCH_START = RS485_ADDRESS_REGISTER;  // Start at register 70
    constexpr uint16_t MODULE_BATCH_COUNT = 7;  // From RS485_ADDRESS_REGISTER through MEASUREMENT_RANGE_REGISTER
    
    LOG_MB8ART_DEBUG_NL("Reading module settings and measurement range");
    auto moduleResult = readHoldingRegisters(MODULE_BATCH_START, MODULE_BATCH_COUNT);
    if (!moduleResult.isOk()) {
        LOG_MB8ART_ERROR_NL("Module batch read failed with error: %d - cannot determine measurement range!", 
                          static_cast<int>(moduleResult.error()));
        return false;  // Critical failure - we need measurement range
    } else if (moduleResult.value().size() < MODULE_BATCH_COUNT) {
        LOG_MB8ART_ERROR_NL("Module batch read returned %d registers, expected %d", 
                          moduleResult.value().size(), MODULE_BATCH_COUNT);
        return false;  // Critical failure
    } else {
        // Process module settings from batch (starting at register 70)
        // Register 70: RS485 address (offset 0)
        moduleSettings.rs485Address = moduleResult.value()[0] & 0xFF;
        
        // Register 71: Baud rate (offset 1)
        moduleSettings.baudRate = moduleResult.value()[1] & 0xFF;
        
        // Register 72: Parity (offset 2)
        moduleSettings.parity = moduleResult.value()[2] & 0xFF;
        
        // Register 76: Measurement range - MB8ART device quirk:
        // In batch reads, the measurement range value appears at register 75 (index 5)
        // even though single register reads show it correctly at register 76
        uint16_t rawRange = moduleResult.value()[5];  // Read from index 5 for batch reads
        LOG_MB8ART_DEBUG_NL("Measurement range at index 5 (reg 75): 0x%04X", rawRange);
        LOG_MB8ART_DEBUG_NL("Value at index 6 (reg 76): 0x%04X", moduleResult.value()[6]);
        
        currentRange = static_cast<mb8art::MeasurementRange>(rawRange & 0x01);
        
        setInitializationBit(InitBits::MEASUREMENT_RANGE);
        
        LOG_MB8ART_DEBUG_NL("Module settings batch read successful");
        LOG_MB8ART_DEBUG_NL("Settings - Addr: 0x%02X, Baud: %s, Range: %s",
                           moduleSettings.rs485Address,
                           baudRateToString(getBaudRateEnum(moduleSettings.baudRate)).c_str(),
                           (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
    }
    
    setInitializationBit(InitBits::DEVICE_RESPONSIVE);
    
    return true;  // Channel configs successful (module settings are optional)
}

bool MB8ART::batchReadInitialConfig() {
    MB8ART_LOG_INIT_STEP("Batch reading device configuration...");
    
    // Read 10 registers starting from module temp (67) through measurement range (76)
    auto result = readHoldingRegisters(MODULE_TEMPERATURE_REGISTER, 10);
    
    if (result.isOk()) {
        LOG_MB8ART_DEBUG_NL("Batch config request sent successfully");
        // The response will be handled in handleModbusResponse
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to send batch config request");
        return false;
    }
}



void MB8ART::readOptionalSettings() {
    // Read RS485 address
    if (reqAddress()) {
        EventBits_t bits = MB8ART_SRP_EVENT_GROUP_WAIT_BITS(
            xTaskEventGroup,
            DATA_READY_BIT | DATA_ERROR_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(500)
        );
        
        if (bits & DATA_READY_BIT) {
            LOG_MB8ART_DEBUG_NL("RS485 address: 0x%02X", moduleSettings.rs485Address);
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Read baud rate
    if (reqBaudRate()) {
        EventBits_t bits = MB8ART_SRP_EVENT_GROUP_WAIT_BITS(
            xTaskEventGroup,
            DATA_READY_BIT | DATA_ERROR_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(500)
        );
        
        if (bits & DATA_READY_BIT) {
            LOG_MB8ART_DEBUG_NL("Baud rate: %s", 
                              baudRateToString(getBaudRateEnum(moduleSettings.baudRate)).c_str());
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Read module temperature
    if (reqModuleTemperature()) {
        EventBits_t bits = MB8ART_SRP_EVENT_GROUP_WAIT_BITS(
            xTaskEventGroup,
            DATA_READY_BIT | DATA_ERROR_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(500)
        );
        
        if (bits & DATA_READY_BIT && moduleSettings.isTemperatureValid) {
            LOG_MB8ART_DEBUG_NL("Module temperature: %.1f°C", moduleSettings.moduleTemperature);
        }
    }
}




void MB8ART::setDataReceiverTask(TaskHandle_t taskHandle) {
    dataReceiverTask = taskHandle;
    LOG_MB8ART_DEBUG_NL("Data receiver task set: %p", taskHandle);
}




bool MB8ART::reqAddress() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqAddress blocked - device offline");
        return false;
    }
    
    // Read the Modbus register at address 0x0046 (70) to get the RS485 address from the holding registers.
    auto result = readHoldingRegisters(RS485_ADDRESS_REGISTER, 1);
    
    if (result.isOk() && !result.value().empty()) {
        LOG_MB8ART_DEBUG_NL("RS485 address request successful, value: 0x%02X", result.value()[0]);
        moduleSettings.rs485Address = result.value()[0] & 0xFF;
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to read RS485 address, error: %d", static_cast<int>(result.error()));
        return false;
    }
}




bool MB8ART::reqBaudRate() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqBaudRate blocked - device offline");
        return false;
    }
    
    LOG_MB8ART_DEBUG_NL("Requesting baud rate from register 0x%02X", BAUD_RATE_REGISTER);
    // Read the Modbus register at address 0x0047 (71) to get the RS485 baud rate configuration from the holding registers.
    auto result = readHoldingRegisters(BAUD_RATE_REGISTER, 1);
    
    if (result.isOk() && !result.value().empty()) {
        LOG_MB8ART_DEBUG_NL("Baud rate request successful, value: %d", result.value()[0]);
        moduleSettings.baudRate = result.value()[0] & 0xFF;
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to read baud rate, error: %d", static_cast<int>(result.error()));
        return false;
    }
}




bool MB8ART::reqParity() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqParity blocked - device offline");
        return false;
    }
    
    LOG_MB8ART_DEBUG_NL("Requesting parity from register 0x%02X", PARITY_REGISTER);
    // Read the Modbus register at address 0x0048 (72) to get the RS485 parity configuration from the holding registers.
    auto result = readHoldingRegisters(PARITY_REGISTER, 1);
    
    if (result.isOk() && !result.value().empty()) {
        LOG_MB8ART_DEBUG_NL("Parity request successful, value: %d", result.value()[0]);
        moduleSettings.parity = result.value()[0] & 0xFF;
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to read parity, error: %d", static_cast<int>(result.error()));
        return false;
    }
}




bool MB8ART::reqModuleTemperature() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqModuleTemperature blocked - device offline");
        return false;
    }
    
    // Read the Modbus register at address 0x0044 (68) for the module temperature
    auto result = readHoldingRegisters(MODULE_TEMPERATURE_REGISTER, 1);
    
    if (result.isOk() && !result.value().empty()) {
        LOG_MB8ART_DEBUG_NL("Module temperature request successful, raw value: %d", result.value()[0]);
        moduleSettings.moduleTemperature = result.value()[0] * 0.1f;  // Convert to degrees
        moduleSettings.isTemperatureValid = true;
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to read module temperature, error: %d", static_cast<int>(result.error()));
        moduleSettings.isTemperatureValid = false;
        return false;
    }
}




bool MB8ART::reqMeasurementRange() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqMeasurementRange blocked - device offline");
        return false;
    }
    
    // Read the Modbus register for measurement range (address 0x004C)
    auto result = readHoldingRegisters(MEASUREMENT_RANGE_REGISTER, 1);
    
    if (result.isOk() && !result.value().empty()) {
        uint16_t value = result.value()[0];
        LOG_MB8ART_DEBUG_NL("Measurement range request successful, value: %d", value);
        currentRange = static_cast<mb8art::MeasurementRange>(value & 0x01);
        return true;
    } else {
        LOG_MB8ART_ERROR_NL("Failed to read measurement range");
        return false;
    }
}




// Request methods
bool MB8ART::reqAllChannelModes() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqAllChannelModes blocked - device offline");
        return false;
    }
    
    // Read all channel configurations
    auto result = readHoldingRegisters(CHANNEL_CONFIG_REGISTER_START, DEFAULT_NUMBER_OF_SENSORS);
    
    if (!result.isOk()) {
        LOG_MB8ART_ERROR_NL("Failed to request all channel configurations");
        return false;
    }
    
    return true;
}




bool MB8ART::reqChannelMode(uint8_t channel) {
    if (channel >= DEFAULT_NUMBER_OF_SENSORS) return false; // Invalid channel index
    
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqChannelMode blocked - device offline");
        return false;
    }

    uint16_t startingAddress = CHANNEL_CONFIG_REGISTER_START + channel;
    
    // Read single channel configuration
    auto result = readHoldingRegisters(startingAddress, 1);
    
    if (!result.isOk()) {
        LOG_MB8ART_ERROR_NL("Failed to request channel %d configuration", channel);
        return false;
    }
    
    return true;
}




// setUpdateEventBits is now in MB8ARTEvents.cpp




// setErrorEventBits is now in MB8ARTEvents.cpp




std::string MB8ART::baudRateToString(BaudRate rate) {
    static const std::unordered_map<BaudRate, std::string> baudRateStrings = {
        {BaudRate::BAUD_1200, "1200 bps"},
        {BaudRate::BAUD_2400, "2400 bps"},
        {BaudRate::BAUD_4800, "4800 bps"},
        {BaudRate::BAUD_9600, "9600 bps"},
        {BaudRate::BAUD_19200, "19200 bps"},
        {BaudRate::BAUD_38400, "38400 bps"},
        {BaudRate::BAUD_57600, "57600 bps"},
        {BaudRate::BAUD_115200, "115200 bps"},
        {BaudRate::BAUD_FACTORY_RESET, "Factory reset"}
    };
    
    auto it = baudRateStrings.find(rate);
    return it != baudRateStrings.end() ? it->second : "Unknown baud rate";
}




std::string MB8ART::parityToString(Parity parity) {
    static const std::unordered_map<Parity, std::string> parityStrings = {
        {Parity::NONE, "None"},
        {Parity::ODD, "Odd"},
        {Parity::EVEN, "Even"},
        {Parity::ERROR, "Error"}
    };
    
    auto it = parityStrings.find(parity);
    return it != parityStrings.end() ? it->second : "Unknown";
}




BaudRate MB8ART::getBaudRateEnum(uint8_t rawValue) {
    static const std::unordered_map<uint8_t, BaudRate> baudRateMap = {
        {0x00, BaudRate::BAUD_1200},
        {0x01, BaudRate::BAUD_2400},
        {0x02, BaudRate::BAUD_4800},
        {0x03, BaudRate::BAUD_9600},
        {0x04, BaudRate::BAUD_19200},
        {0x05, BaudRate::BAUD_38400},
        {0x06, BaudRate::BAUD_57600},
        {0x07, BaudRate::BAUD_115200}
    };
    
    auto it = baudRateMap.find(rawValue);
    return it != baudRateMap.end() ? it->second : BaudRate::ERROR;
}




Parity MB8ART::getParityEnum(uint8_t rawValue) {
    static const std::unordered_map<uint8_t, Parity> parityMap = {
        {0, Parity::NONE},
        {1, Parity::EVEN},
        {2, Parity::ODD}
    };
    
    auto it = parityMap.find(rawValue);
    return it != parityMap.end() ? it->second : Parity::ERROR;
}





// setInitializationBit remains in MB8ART.cpp (initialization logic)



    
// requestConnectionStatus remains in MB8ART.cpp (core functionality)




void MB8ART::setTag(const char* newTag) {
    tag = newTag;
}

// ========== Unified Mapping API ==========

void MB8ART::bindSensorPointers(const std::array<mb8art::SensorBinding, 8>& bindings) {
    LOG_MB8ART_DEBUG_NL("Binding sensor pointers (unified mapping API)");

    // Copy the binding array
    sensorBindings = bindings;

    // Log which sensors have bindings
    for (size_t i = 0; i < 8; i++) {
        if (bindings[i].temperaturePtr != nullptr && bindings[i].validityPtr != nullptr) {
            LOG_MB8ART_DEBUG_NL("Sensor %d bound to temp=0x%p, valid=0x%p",
                               i, bindings[i].temperaturePtr, bindings[i].validityPtr);
        } else {
            LOG_MB8ART_DEBUG_NL("Sensor %d has incomplete binding (nullptr)", i);
        }
    }
}

void MB8ART::setHardwareConfig(const mb8art::SensorHardwareConfig* config) {
    LOG_MB8ART_DEBUG_NL("Setting hardware configuration (unified mapping API)");

    if (config == nullptr) {
        LOG_MB8ART_ERROR_NL("Hardware config pointer is null!");
        return;
    }

    // Store pointer to constexpr config array (lives in flash)
    this->hardwareConfig = config;

    LOG_MB8ART_DEBUG_NL("Hardware config set successfully (constexpr array in flash)");
}


