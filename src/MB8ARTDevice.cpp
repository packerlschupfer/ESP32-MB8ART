/**
 * @file MB8ARTDevice.cpp
 * @brief IDeviceInstance interface implementation
 * 
 * This file contains ideviceinstance interface implementation for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>
#include <string.h>

using namespace mb8art;

bool MB8ART::probeDevice() {
    LOG_MB8ART_DEBUG_NL("Probing device at address 0x%02X", getServerAddress());

    // Clear any pending responses before probing
    clearPendingResponses();

    // Single read to verify device is responsive
    // Use measurement range register - small, fast, and confirms device identity
    auto result = readHoldingRegisters(MEASUREMENT_RANGE_REGISTER, 1);

    if (result.isOk() && !result.value().empty()) {
        LOG_MB8ART_DEBUG_NL("Device probe successful");
        statusFlags.moduleOffline = 0;  // Device is online
        return true;
    } else {
        LOG_MB8ART_DEBUG_NL("Device probe failed");
        statusFlags.moduleOffline = 1;  // Mark device as offline
        return false;
    }
}

// SimpleModbusDevice interface implementation
// readChannelData was used by SimpleModbusDevice - no longer needed with QueuedModbusDevice
bool MB8ART::readChannelData() {
    // With QueuedModbusDevice, data is handled asynchronously through onAsyncResponse
    // This method is kept for backward compatibility but doesn't do anything
    LOG_MB8ART_WARN_NL("readChannelData() called - this method is deprecated with QueuedModbusDevice");
    return true;
}

IDeviceInstance::DeviceResult<void> MB8ART::waitForInitializationComplete(TickType_t timeout) {
    if (!xInitEventGroup) {
        LOG_MB8ART_ERROR_NL("Initialization event group not created");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }

    // First immediate check
    if (checkAllInitBitsSet()) {
        LOG_MB8ART_DEBUG_NL("Device already initialized");
        return IDeviceInstance::DeviceResult<void>();
    }

    LOG_MB8ART_DEBUG_NL("Waiting for initialization to complete (timeout: %lu ms)", timeout);
    
    // Wait for all initialization bits
    EventBits_t result = MB8ART_SRP_EVENT_GROUP_WAIT_BITS(
        xInitEventGroup,
        InitBits::ALL_BITS,
        pdFALSE,
        pdTRUE,
        timeout
    );

    if ((result & InitBits::ALL_BITS) == InitBits::ALL_BITS) {
        LOG_MB8ART_DEBUG_NL("All initialization bits set - device ready");
        statusFlags.initialized = true;
        return IDeviceInstance::DeviceResult<void>();
    }

    // Analyze missing bits
    EventBits_t missingBits = (~result) & InitBits::ALL_BITS;
    LOG_MB8ART_ERROR_NL("Missing initialization bits: 0x%02X", missingBits);
    
    if (missingBits & InitBits::DEVICE_RESPONSIVE) {
        LOG_MB8ART_ERROR_NL("Missing: Device Responsive");
    }
    if (missingBits & InitBits::MEASUREMENT_RANGE) {
        LOG_MB8ART_ERROR_NL("Missing: Measurement Range");
    }
    if (missingBits & InitBits::CHANNEL_CONFIG) {
        LOG_MB8ART_ERROR_NL("Missing: Channel Configuration");
    }

    return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::TIMEOUT);
}

bool MB8ART::waitForData() {
    // Call the overloaded version with default timeout
    return waitForData(pdMS_TO_TICKS(1000)) == IDeviceInstance::DeviceError::SUCCESS;
}

IDeviceInstance::DeviceError MB8ART::waitForData(TickType_t timeout) {
    if (!xUpdateEventGroup || !xErrorEventGroup) {
        LOG_MB8ART_ERROR_NL("Event groups not initialized");
        return IDeviceInstance::DeviceError::NOT_INITIALIZED;
    }

    // Calculate active channel mask based on configured channels
    EventBits_t activeChannelMask = 0;
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            activeChannelMask |= (1 << i);
        }
    }

    if (activeChannelMask == 0) {
        LOG_MB8ART_WARN_NL("No active channels configured");
        return IDeviceInstance::DeviceError::INVALID_PARAMETER;
    }

    LOG_MB8ART_DEBUG_NL("Waiting for data with active channel mask: 0x%06X", activeChannelMask);

    // Wait for update bits corresponding to active channels
    EventBits_t updateBits = MB8ART_SRP_EVENT_GROUP_WAIT_BITS(
        xUpdateEventGroup,
        activeChannelMask,
        pdTRUE,     // Clear bits on exit
        pdTRUE,     // Wait for all bits
        timeout
    );

    if (updateBits & activeChannelMask) {
        LOG_MB8ART_DEBUG_NL("Received update bits: 0x%06X", updateBits);

        // Check for errors on the same channels
        EventBits_t errorBits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xErrorEventGroup);
        LOG_MB8ART_DEBUG_NL("Current error bits: 0x%06X", errorBits);

        // Process each channel
        for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            EventBits_t channelBit = (1 << i);
            if (activeChannelMask & channelBit) {
                if (updateBits & channelBit) {
                    LOG_MB8ART_DEBUG_NL("Channel %d data updated (bit 0x%06X)", i, channelBit);
                }
                if (errorBits & channelBit) {
                    LOG_MB8ART_WARN_NL("Channel %d error detected (bit 0x%06X)", i, channelBit);
                }
            }
        }

        LOG_MB8ART_DEBUG_NL("Successfully received updates for active channels");
        return IDeviceInstance::DeviceError::SUCCESS;
    }

    LOG_MB8ART_ERROR_NL("Timeout waiting for sensor data (mask: 0x%06X)", activeChannelMask);
    return IDeviceInstance::DeviceError::TIMEOUT;
}

IDeviceInstance::DeviceResult<void> MB8ART::processData() {
    LOG_MB8ART_DEBUG_NL("Processing sensor data");

    bool anyDataProcessed = false;

    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; ++i) {
        if (channelConfigs[i].mode == static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            continue;
        }

        if (sensorReadings[i].isTemperatureValid && sensorReadings[i].lastCommandSuccess) {
            anyDataProcessed = true;
        }
    }

    if (!anyDataProcessed) {
        LOG_MB8ART_DEBUG_NL("No valid sensor data to process");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }

    return IDeviceInstance::DeviceResult<void>();
}

IDeviceInstance::DeviceResult<void> MB8ART::requestData() {
    if (!statusFlags.initialized) {
        LOG_MB8ART_ERROR_NL("Cannot request data before initialization");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }
    
    // Check if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_ERROR_NL("Cannot request data - device is offline");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }

    MB8ART_PERF_START(request_data);
    
    // Clear any existing bits before starting new request
    EventBits_t activeBitsMask = 0;
    int activeChannelCount = 0;
    
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            activeBitsMask |= (1 << i);
            activeChannelCount++;
        }
    }
    
    if (activeChannelCount == 0) {
        LOG_MB8ART_WARN_NL("No active channels configured");
        MB8ART_PERF_END(request_data, "No active channels");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }

    // Clear bits for active channels only
    if (xUpdateEventGroup) {
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xUpdateEventGroup, activeBitsMask);
    }
    if (xErrorEventGroup) {
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xErrorEventGroup, activeBitsMask);
    }
    
    // Request all temperatures at once (batch read)
    auto result = reqTemperatures(DEFAULT_NUMBER_OF_SENSORS);
    
    MB8ART_PERF_END(request_data, "Data request");
    
    if (result.isOk()) {
        return IDeviceInstance::DeviceResult<void>();
    } else {
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::configureMeasurementRange(mb8art::MeasurementRange range) {
    if (!statusFlags.initialized) {
        LOG_MB8ART_ERROR_NL("Cannot configure measurement range before initialization");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }
    
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_ERROR_NL("Cannot configure measurement range - device is offline");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }

    MB8ART_PERF_START(config_range);

    uint16_t dataToWrite = static_cast<uint16_t>(range);
    
    // Use synchronous write from base class
    auto result = writeSingleRegister(MEASUREMENT_RANGE_REGISTER, dataToWrite);

    if (result.isOk()) {
        currentRange = range;
        LOG_MB8ART_INFO_NL("Measurement range configured to: %s",
                         (range == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
       
        MB8ART_PERF_END(config_range, "Range configuration");
        return IDeviceInstance::DeviceResult<void>();
    } else {
        LOG_MB8ART_ERROR_NL("Failed to configure measurement range");
        MB8ART_PERF_END(config_range, "Range configuration failed");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::configureChannelMode(uint8_t channel, uint16_t mode) {
    if (!statusFlags.initialized) {
        LOG_MB8ART_ERROR_NL("Cannot configure channel before initialization");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }
    
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_ERROR_NL("Cannot configure channel - device is offline");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }

    if (channel >= DEFAULT_NUMBER_OF_SENSORS) {
        LOG_MB8ART_ERROR_NL("Invalid channel index: %d", channel);
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }

    MB8ART_PERF_START(config_channel);

    // Calculate register address (128 = 0x80 is the start)
    uint16_t registerAddress = CHANNEL_CONFIG_REGISTER_START + channel;

    // Extract mode and subtype from the configuration
    uint8_t channelMode = (mode & 0xFF00) >> 8;  // High byte
    uint8_t subType = mode & 0x00FF;             // Low byte

    // Validate mode
    if (!validateChannelConfig(channelMode, subType)) {
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }

    // Use synchronous write from base class
    auto result = writeSingleRegister(registerAddress, mode);

    if (result.isOk()) {
        channelConfigs[channel].mode = channelMode;
        channelConfigs[channel].subType = subType;
        
        // Mark sensor as requiring update
        sensorReadings[channel].lastCommandSuccess = true;
        sensorReadings[channel].isStateConfirmed = false;
        
        LOG_MB8ART_DEBUG_NL("Channel %d configured: Mode=%s, SubType=%s", 
                          channel,
                          mb8art::channelModeToString(static_cast<mb8art::ChannelMode>(channelMode)),
                          getSubTypeString(static_cast<mb8art::ChannelMode>(channelMode), subType));
        
        MB8ART_PERF_END(config_channel, "Channel configuration");
        return IDeviceInstance::DeviceResult<void>();
    }

    LOG_MB8ART_ERROR_NL("Failed to configure channel");
    sensorReadings[channel].lastCommandSuccess = false;
    return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
}

IDeviceInstance::DeviceResult<void> MB8ART::configureAllChannels(mb8art::ChannelMode mode, uint16_t subType) {
    LOG_MB8ART_DEBUG_NL("Configuring all channels to mode %s (batch write)", mb8art::channelModeToString(mode));
    
    MB8ART_PERF_START(config_all_channels);
    
    // Prepare configuration value
    uint16_t config = (static_cast<uint16_t>(mode) << 8) | subType;
    
    // Use writeMultipleRegisters for batch writing
    std::vector<uint16_t> values(DEFAULT_NUMBER_OF_SENSORS, config);
    auto result = writeMultipleRegisters(CHANNEL_CONFIG_REGISTER_START, values);
    
    if (result.isOk()) {
        // Update local cache
        for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            channelConfigs[i].mode = static_cast<uint16_t>(mode);
            channelConfigs[i].subType = subType;
        }
        
        // Update active channel mask based on new configuration
        updateActiveChannelMask();
        
        LOG_MB8ART_DEBUG_NL("✓ All channels configured successfully in single transaction");
        MB8ART_PERF_END(config_all_channels, "Batch channel configuration");
        return IDeviceInstance::DeviceResult<void>();
    } else {
        LOG_MB8ART_ERROR_NL("Failed to write batch channel configuration");
        MB8ART_PERF_END(config_all_channels, "Batch channel configuration (failed)");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::configureChannelRange(uint8_t startChannel, uint8_t endChannel, 
                                   mb8art::ChannelMode mode, uint16_t subType) {
    if (startChannel >= DEFAULT_NUMBER_OF_SENSORS || 
        endChannel >= DEFAULT_NUMBER_OF_SENSORS || 
        startChannel > endChannel) {
        LOG_MB8ART_ERROR_NL("Invalid channel range: %d-%d", startChannel, endChannel);
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }
    
    MB8ART_PERF_START(config_range);
    
    uint8_t channelCount = endChannel - startChannel + 1;
    uint16_t config = (static_cast<uint16_t>(mode) << 8) | subType;
    
    LOG_MB8ART_DEBUG_NL("Configuring channels %d-%d to mode %s (batch write)", 
                       startChannel, endChannel, mb8art::channelModeToString(mode));
    
    // Use writeMultipleRegisters for batch writing
    uint16_t startRegister = static_cast<uint16_t>(CHANNEL_CONFIG_REGISTER_START + startChannel);
    std::vector<uint16_t> values(channelCount, config);
    auto result = writeMultipleRegisters(startRegister, values);
    
    if (result.isOk()) {
        // Update local cache
        for (uint8_t i = startChannel; i <= endChannel; i++) {
            channelConfigs[i].mode = static_cast<uint16_t>(mode);
            channelConfigs[i].subType = subType;
        }
        
        // Update active channel mask based on new configuration
        updateActiveChannelMask();
        
        LOG_MB8ART_DEBUG_NL("✓ Channels %d-%d configured successfully in single transaction", 
                           startChannel, endChannel);
        MB8ART_PERF_END(config_range, "Batch channel range configuration");
        return IDeviceInstance::DeviceResult<void>();
    } else {
        LOG_MB8ART_ERROR_NL("Failed to write batch channel range configuration");
        MB8ART_PERF_END(config_range, "Batch channel range configuration (failed)");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::requestAllData() {
    if (!statusFlags.initialized) {
        LOG_MB8ART_ERROR_NL("Cannot request data before initialization");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }
    
    // Check if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("requestAllData blocked - device is offline");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    // Clear event bits
    if (xTaskEventGroup) {
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xTaskEventGroup, DATA_READY_BIT | DATA_ERROR_BIT);
    }
    
    // Request connection status first (optional)
    // Read discrete inputs for connection status
    auto result = readDiscreteInputs(CONNECTION_STATUS_START_REGISTER, DEFAULT_NUMBER_OF_SENSORS);
    if (!result.isOk()) {
        LOG_MB8ART_WARN_NL("Failed to request connection status");
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Request temperature data - this already reads all channels at once
    auto tempResult = reqTemperatures(DEFAULT_NUMBER_OF_SENSORS);
    
    // Optionally request module temperature
    if (tempResult.isOk()) {
        vTaskDelay(pdMS_TO_TICKS(20));
        reqModuleTemperature();
    }
    
    return tempResult;
}

IDeviceInstance::DeviceResult<void> MB8ART::reqTemperatures(int numberOfSensors, bool highResolution) {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("reqTemperatures blocked - device is offline");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    uint16_t count = numberOfSensors;
    MB8ART_PERF_START(req_temps);
    
    if (count == 0 || count > DEFAULT_NUMBER_OF_SENSORS) {
        LOG_MB8ART_ERROR_NL("Invalid temperature count: %d", count);
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }

    // Read all sensor temperatures in one batch - 8 registers starting at 0
    // Use SENSOR priority (safety-critical data)
    auto result = readInputRegistersWithPriority(0, count, esp32Modbus::SENSOR);

    MB8ART_PERF_END(req_temps, "Request temperatures");
    
    if (result.isOk()) {
        // Commented out to reduce log spam - called every 2.5 seconds
        // LOG_MB8ART_DEBUG_NL("Temperature request sent for %d sensors", count);
        return IDeviceInstance::DeviceResult<void>();
    } else {
        LOG_MB8ART_ERROR_NL("Failed to request temperatures");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::performAction(int actionId, int actionParam) {
    // MB8ART is a sensor device - no actions needed
    LOG_MB8ART_WARN_NL("performAction called but not implemented for sensor device");
    return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
}

IDeviceInstance::DeviceResult<std::vector<float>> MB8ART::getData(IDeviceInstance::DeviceDataType dataType) {
    if (!statusFlags.initialized) {
        LOG_MB8ART_ERROR_NL("getData called before initialization complete");
        return IDeviceInstance::DeviceResult<std::vector<float>>(IDeviceInstance::DeviceError::NOT_INITIALIZED);
    }

    switch (dataType) {
        case IDeviceInstance::DeviceDataType::TEMPERATURE: {
            // Verify we have valid data
            int validCount = 0;
            int activeCount = 0;
            for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
                if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
                    activeCount++;
                    if (sensorReadings[i].isTemperatureValid) {
                        validCount++;
                    }
                }
            }

            if (activeCount == 0) {
                LOG_MB8ART_ERROR_NL("No active channels configured");
                return IDeviceInstance::DeviceResult<std::vector<float>>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
            }

            if (validCount == 0) {
                LOG_MB8ART_ERROR_NL("No valid sensor data available");
                return IDeviceInstance::DeviceResult<std::vector<float>>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
            }

            // Build vector of temperature values
            std::vector<float> temperatures;
            temperatures.reserve(DEFAULT_NUMBER_OF_SENSORS);
            
            for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
                if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
                    temperatures.push_back(sensorReadings[i].temperature);
                }
            }
            
            // Commented out to reduce log spam - called too frequently
            // LOG_MB8ART_DEBUG_NL("Data collection - Active: %d, Valid: %d, Success: %s", 
            //                    activeCount, validCount, validCount > 0 ? "YES" : "NO");
            
            return IDeviceInstance::DeviceResult<std::vector<float>>::ok(temperatures);
        }
        
        default:
            LOG_MB8ART_ERROR_NL("Unsupported data type requested: %d", static_cast<int>(dataType));
            return IDeviceInstance::DeviceResult<std::vector<float>>(IDeviceInstance::DeviceError::INVALID_PARAMETER);
    }
}

IDeviceInstance::DeviceResult<void> MB8ART::initialize() {
    // Implementation is in MB8ART.cpp as it's the main initialization
    bool success = initializeDevice();

    if (success) {
        lastError = IDeviceInstance::DeviceError::SUCCESS;
        // Use default constructor for success (NOT Result(SUCCESS) which creates error result!)
        return IDeviceInstance::DeviceResult<void>();
    }
    
    // Determine specific error based on device state
    if (statusFlags.moduleOffline) {
        lastError = IDeviceInstance::DeviceError::COMMUNICATION_ERROR;
        LOG_MB8ART_ERROR_NL("Device initialization failed - device is offline/unresponsive");
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
    }
    
    // Default to COMMUNICATION_ERROR for any initialization failure
    lastError = IDeviceInstance::DeviceError::COMMUNICATION_ERROR;
    return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::COMMUNICATION_ERROR);
}

void MB8ART::waitForInitialization() {
    // Wait indefinitely for initialization to complete
    waitForInitializationComplete(portMAX_DELAY);
}