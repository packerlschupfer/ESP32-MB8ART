/**
 * @file MB8ARTModbus.cpp
 * @brief Modbus communication and response handling
 * 
 * This file contains modbus communication and response handling for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>
#include <ModbusErrorTracker.h>


using namespace mb8art;

void MB8ART::clearPendingResponses() {
    // Clear any pending async responses if queue exists
    if (isAsyncEnabled()) {
        // Process and discard all queued packets
        size_t cleared = processQueue();
        if (cleared > 0) {
            LOG_MB8ART_DEBUG_NL("Cleared %d pending async responses", cleared);
        }
    }
    
    // If there were any pending synchronous responses, they would be handled
    // by the ModbusDevice base class using responseSem.
    LOG_MB8ART_DEBUG_NL("Ready for fresh responses");
}

void MB8ART::processModbusResponse(uint8_t functionCode, const uint8_t* data, uint16_t length) {
    LOG_MB8ART_DEBUG_NL("Modbus response received: FC=%d, Length=%d", functionCode, length);

    if (modbusResponseCallback) {
        modbusResponseCallback(functionCode, data, length);
    }
}

void MB8ART::registerModbusResponseCallback(std::function<void(uint8_t, const uint8_t*, uint16_t)> callback) {
    modbusResponseCallback = callback;
}



// Remove waitForInitialization - no longer needed with new architecture

// Handle Modbus responses
void MB8ART::onAsyncResponse(uint8_t functionCode, uint16_t address,
                            const uint8_t* data, size_t length) {
    // For QueuedModbusDevice, onAsyncResponse is called for async responses
    // We just forward to our existing handler
    handleModbusResponse(functionCode, address, data, length);
}




void MB8ART::handleModbusResponse(uint8_t functionCode, uint16_t startingAddress,
                                 const uint8_t* data, size_t length) {
    // Update response time on ANY successful response (passive monitoring)
    lastResponseTime = xTaskGetTickCount();

    // Reset timeout counter - module is responsive
    consecutiveTimeouts = 0;

    // Record successful Modbus operation for diagnostics
    modbus::ModbusErrorTracker::recordSuccess(getServerAddress());

    // Clear offline flag if it was set (device is back online)
    if (statusFlags.moduleOffline) {
        statusFlags.moduleOffline = 0;
        LOG_MB8ART_INFO_NL("Module back ONLINE - received valid response");
    }

    if (data == nullptr || length == 0) {
        LOG_MB8ART_ERROR_NL("Invalid response data");
        return;
    }

    esp32Modbus::FunctionCode fc = static_cast<esp32Modbus::FunctionCode>(functionCode);

    MB8ART_DEBUG_ONLY(
        LOG_MB8ART_DEBUG_NL("handleModbusResponse: FC=%d, Addr=0x%04X, Len=%d, Initialized=%s", 
                         functionCode, startingAddress, length, statusFlags.initialized ? "YES" : "NO");
        MB8ART_LOG_MODBUS_PACKET("RX Data", data, length);
    );

    // During initialization (CONFIGURING phase), responses are expected
    if (getInitPhase() == InitPhase::CONFIGURING) {
        LOG_MB8ART_DEBUG_NL("Processing response during configuration phase");
    }

    switch (fc) {
        case esp32Modbus::FunctionCode::READ_HOLD_REGISTER: {
            MB8ART_LOG_CRITICAL_ENTRY("READ_HOLD_REGISTER processing");
            
            // Any successful read response indicates the device is responsive
            setInitializationBit(InitBits::DEVICE_RESPONSIVE);
            
            // Check if this is a batch read response (7 registers starting at 70)
            if (startingAddress == 70 && length == 14) {  // 7 registers * 2 bytes
                LOG_MB8ART_DEBUG_NL("Processing batch configuration data");
                
                // Parse all values from the batch (starting at register 70)
                // RS485 Address (register 70) - bytes 0-1
                uint16_t rawAddress = (data[0] << 8) | data[1];
                moduleSettings.rs485Address = rawAddress & 0xFF;  // Only low byte is used
                LOG_MB8ART_DEBUG_NL("RS485 address: %d (raw: 0x%04X)", moduleSettings.rs485Address, rawAddress);
                
                // Baud rate (register 71) - bytes 2-3
                uint16_t rawBaud = (data[2] << 8) | data[3];
                moduleSettings.baudRate = rawBaud & 0xFF;  // Only low byte is used
                LOG_MB8ART_DEBUG_NL("Baud rate code: %d (raw: 0x%04X)", moduleSettings.baudRate, rawBaud);
                
                // Parity (register 72) - bytes 4-5
                uint16_t rawParity = (data[4] << 8) | data[5];
                moduleSettings.parity = rawParity & 0xFF;  // Only low byte is used
                LOG_MB8ART_DEBUG_NL("Parity code: %d (raw: 0x%04X)", moduleSettings.parity, rawParity);
                
                // Skip reserved registers 73-74 (bytes 6-9)
                
                // Measurement range - MB8ART device quirk:
                // In batch reads, the value appears at register 75 (bytes 10-11)
                // even though single register reads show it at register 76
                uint16_t rawRange = (data[10] << 8) | data[11];  // Register 75
                currentRange = static_cast<mb8art::MeasurementRange>(rawRange & 0x01);
                LOG_MB8ART_DEBUG_NL("Measurement range from reg 75: %d (raw: 0x%04X)", (int)currentRange, rawRange);
                
                // Log what's at register 76 for debugging
                LOG_MB8ART_DEBUG_NL("Value at reg 76: 0x%04X", (data[12] << 8) | data[13]);
                
                LOG_MB8ART_DEBUG_NL("Batch config received - Range: %s, Addr: %d, Baud: %s",
                                  (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES",
                                  moduleSettings.rs485Address,
                                  baudRateToString(getBaudRateEnum(moduleSettings.baudRate)).c_str());
                
                // Set initialization bit for measurement range
                setInitializationBit(InitBits::MEASUREMENT_RANGE);
                MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                
                MB8ART_LOG_CRITICAL_EXIT("READ_HOLD_REGISTER processing (batch)");
                break;  // Exit switch - IMPORTANT!
            }
            
            // Continue with existing single-register handlers...
            switch (startingAddress) {
                case CONNECTION_STATUS_START_REGISTER: {
                    // Connection status register - used during probe
                    LOG_MB8ART_DEBUG_NL("Connection status register response received");
                    if (!statusFlags.initialized) {
                        setInitializationBit(InitBits::DEVICE_RESPONSIVE);
                    }
                    break;
                }
                
                case MEASUREMENT_RANGE_REGISTER: {
                    if (validatePacketLength(length, EXPECTED_MEASUREMENT_RANGE_PACKET_LENGTH, "Measurement Range")) {
                        uint16_t rawRange = (data[0] << 8) | data[1];
                        currentRange = static_cast<mb8art::MeasurementRange>(rawRange & 0x01);
                        LOG_MB8ART_DEBUG_NL("Measurement Range successfully read: %s",
                                         (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
                        
                        // Only set the MEASUREMENT_RANGE init bit during initialization
                        if (!statusFlags.initialized) {
                            setInitializationBit(InitBits::MEASUREMENT_RANGE);
                        }
                        
                        MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                    }
                    break;
                }

                case CHANNEL_CONFIG_REGISTER_START ... (CHANNEL_CONFIG_REGISTER_START + DEFAULT_NUMBER_OF_SENSORS - 1): {
                    uint8_t channelStart = startingAddress - CHANNEL_CONFIG_REGISTER_START;
                    LOG_MB8ART_DEBUG_NL("Channel configuration packet received, starting at channel %d, length=%d", 
                                       channelStart, length);
                    if (length % 2 == 0 && (channelStart + length / 2) <= DEFAULT_NUMBER_OF_SENSORS) {
                        for (uint8_t i = 0; i < length / 2; i++) {
                            uint8_t channel = channelStart + i;
                            uint16_t rawConfig = (data[i * 2] << 8) | data[i * 2 + 1];
                            processChannelConfig(channel, rawConfig);
                        }
                        
                        // During initialization, handle both single and multi-channel reads
                        if (!statusFlags.initialized) {
                            // Mark channels as configured
                            int numChannels = length / 2;
                            for (int ch = 0; ch < numChannels; ch++) {
                                channelsConfiguredDuringInit |= (1 << (channelStart + ch));
                            }
                            
                            LOG_MB8ART_DEBUG_NL("Configured %d channel(s) starting at %d during init", 
                                               numChannels, channelStart);
                            
                            // Check if we've received all 8 channels
                            if (channelsConfiguredDuringInit == 0xFF) {  // All 8 bits set
                                LOG_MB8ART_DEBUG_NL("All channels configured during init (0x%02X)", 
                                                  channelsConfiguredDuringInit);
                                setInitializationBit(InitBits::CHANNEL_CONFIG);
                            } else {
                                LOG_MB8ART_DEBUG_NL("Channels configured so far: 0x%02X", 
                                                   channelsConfiguredDuringInit);
                            }
                        } else if (channelStart == 0 && length == (DEFAULT_NUMBER_OF_SENSORS * 2)) {
                            // All channels in one response (normal operation)
                            LOG_MB8ART_DEBUG_NL("All %d channels configured in single response", DEFAULT_NUMBER_OF_SENSORS);
                            setInitializationBit(InitBits::CHANNEL_CONFIG);
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                        } else if (channelStart + length / 2 == DEFAULT_NUMBER_OF_SENSORS) {
                            // Multi-packet that completes at channel 8
                            LOG_MB8ART_DEBUG_NL("Channel configuration complete (multi-packet)");
                            setInitializationBit(InitBits::CHANNEL_CONFIG);
                        }
                    } else {
                        LOG_MB8ART_WARN_NL("Invalid channel config packet: start=%d, length=%d", 
                                          channelStart, length);
                    }
                    break;
                }

                case RS485_ADDRESS_REGISTER: {
                    LOG_MB8ART_DEBUG_NL("RS485 Address packet received, length=%d", length);
                    if (validatePacketLength(length, EXPECTED_RS485_PACKET_LENGTH, "RS485 Address")) {
                        uint16_t rs485AddressSetting = (data[0] << 8) | data[1];
                        moduleSettings.rs485Address = rs485AddressSetting;
                        LOG_MB8ART_DEBUG_NL("RS485 Address successfully read: %d", rs485AddressSetting);
                        MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                    }
                    break;
                }

                case BAUD_RATE_REGISTER: {
                    LOG_MB8ART_DEBUG_NL("Baud Rate packet received, length=%d", length);
                    if (validatePacketLength(length, EXPECTED_BAUD_RATE_PACKET_LENGTH, "Baud Rate")) {
                        uint16_t rawBaudRate = (data[0] << 8) | data[1];
                        if (rawBaudRate <= MAX_BAUD_RATE_VALUE) {
                            moduleSettings.baudRate = rawBaudRate;
                            LOG_MB8ART_DEBUG_NL("RS485 Baud Rate successfully read: %s",
                                             baudRateToString(getBaudRateEnum(rawBaudRate)).c_str());
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                        } else {
                            LOG_MB8ART_ERROR_NL("Invalid Baud Rate value: %d", rawBaudRate);
                        }
                    }
                    break;
                }

                case PARITY_REGISTER: {
                    LOG_MB8ART_DEBUG_NL("Parity packet received, length=%d", length);
                    if (validatePacketLength(length, EXPECTED_PARITY_PACKET_LENGTH, "Parity")) {
                        uint16_t rawParity = (data[0] << 8) | data[1];
                        if (rawParity <= MAX_PARITY_VALUE) {
                            moduleSettings.parity = rawParity;
                            LOG_MB8ART_DEBUG_NL("RS485 Parity successfully read: %s",
                                             parityToString(getParityEnum(rawParity)).c_str());
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                        } else {
                            LOG_MB8ART_ERROR_NL("Invalid Parity value: %d", rawParity);
                        }
                    }
                    break;
                }

                case MODULE_TEMPERATURE_REGISTER: {
                    LOG_MB8ART_DEBUG_NL("Module temperature packet received, length=%d", length);
                    if (validatePacketLength(length, EXPECTED_MODULE_TEMP_PACKET_LENGTH, "Module Temperature")) {
                        uint16_t rawTemperature = (data[0] << 8) | data[1];
                        float moduleTemperature = rawTemperature * 0.1f;
                        moduleSettings.moduleTemperature = moduleTemperature;
                        moduleSettings.isTemperatureValid = true;
                        LOG_MB8ART_DEBUG_NL("Module Temperature successfully read: %.1f°C", moduleTemperature);
                        MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                    }
                    break;
                }

                default:
                    LOG_MB8ART_WARN_NL("Unhandled holding register: 0x%04X", startingAddress);
                    break;
            }
            
            // Check if all initialization is complete (silently during init)
            if (!statusFlags.initialized && xInitEventGroup) {
                EventBits_t bits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
                bool allSet = (bits & InitBits::ALL_BITS) == InitBits::ALL_BITS;
                
                if (allSet) {
                    LOG_MB8ART_INFO_NL("All initialization bits set, device fully initialized");
                    statusFlags.initialized = 1;
                    
                    // Signal initialization complete to tasks
                    if (xTaskEventGroup) {
                        MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, INIT_COMPLETE_BIT);
                    }
                    
                }
            }

            MB8ART_LOG_CRITICAL_EXIT("READ_HOLD_REGISTER processing");
            break;
        }

        case esp32Modbus::FunctionCode::READ_DISCR_INPUT: {
            if (startingAddress == CONNECTION_STATUS_START_REGISTER) {
                LOG_MB8ART_DEBUG_NL("Connection status data received!");
                handleConnectionStatus(data, length);
            } else {
                LOG_MB8ART_WARN_NL("Unhandled READ_DISCR_INPUT starting address: %d", startingAddress);
            }
            break;
        }

        case esp32Modbus::FunctionCode::READ_INPUT_REGISTER: {
            switch (startingAddress) {
                case TEMPERATURE_REGISTER_START: { // Address range for temperature data
                    MB8ART_PERF_START(temp_processing);

                    // Update global timestamp for fast path
                    lastGlobalDataUpdate = xTaskGetTickCount();

                    LOG_MB8ART_DEBUG_NL("Temperature data packet received, length=%d", length);

                    // Confirm the current measurement range
                    LOG_MB8ART_DEBUG_NL("Current Measurement Range: %s",
                                     (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");

                    // Validate packet length
                    if (!validatePacketLength(length, EXPECTED_TEMPERATURE_PACKET_LENGTH, "Temperature Data")) {
                        // Set error bits for all sensors (interleaved format)
                        MB8ART_SRP_EVENT_GROUP_SET_BITS(xSensorEventGroup, mb8art::ALL_SENSOR_ERROR_BITS);
                        
                        // Notify tasks of error
                        if (xTaskEventGroup) {
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_ERROR_BIT);
                        }
                        if (dataReceiverTask) {
                            xTaskNotify(dataReceiverTask, DATA_ERROR_BIT, eSetBits);
                        }
                        return;
                    }

                    EventBits_t updateBitsToSet = 0;
                    EventBits_t errorBitsToSet = 0;
                    EventBits_t errorBitsToClear = 0;
                    char statusBuffer[256];  // Thread-local buffer for thread safety
                    statusBuffer[0] = '\0';  // Clear buffer
                    
                    processTemperatureData(data, length, updateBitsToSet, errorBitsToSet, 
                                          errorBitsToClear, statusBuffer, sizeof(statusBuffer));
                    
                    updateEventBits(updateBitsToSet, errorBitsToSet, errorBitsToClear);
                    
                    // Notify waiting tasks if we have valid data
                    if (updateBitsToSet) {
                        // Set event bit for any waiting tasks
                        if (xTaskEventGroup) {
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                        }
                        
                        // Direct notification to data receiver
                        notifyDataReceiver();
                    }
                    
                    if (errorBitsToSet) {
                        if (xTaskEventGroup) {
                            MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_ERROR_BIT);
                        }
                        if (dataReceiverTask) {
                            xTaskNotify(dataReceiverTask, DATA_ERROR_BIT, eSetBits);
                        }
                    }
                    
                    // Only log if there's something to log
                    if (statusBuffer[0] != '\0') {
                        LOG_MB8ART_DEBUG_NL("%s", statusBuffer);
                    }
                    
                    MB8ART_PERF_END(temp_processing, "Temperature processing");
                    break;
                }
                
                default:
                    LOG_MB8ART_WARN_NL("Unhandled READ_INPUT_REGISTER starting address: %d", startingAddress);
                    break;
            }
            break;
        }

        case esp32Modbus::FunctionCode::WRITE_HOLD_REGISTER: {
            // With the fix, we now get 4 bytes: 2 for address, 2 for value
            if (length >= 4) {
                LOG_MB8ART_DEBUG_NL("Write register acknowledged - Address: 0x%04X, Value: 0x%04X", 
                                   (data[0] << 8) | data[1], (data[2] << 8) | data[3]);
            } else {
                LOG_MB8ART_DEBUG_NL("Write register acknowledged at address: 0x%04X", startingAddress);
            }
            
            // For configuration writes, set appropriate bits
            if (startingAddress >= CHANNEL_CONFIG_REGISTER_START && 
                startingAddress < CHANNEL_CONFIG_REGISTER_START + DEFAULT_NUMBER_OF_SENSORS) {
                uint8_t channel = startingAddress - CHANNEL_CONFIG_REGISTER_START;
                EventBits_t sensorUpdateBit = mb8art::SENSOR_UPDATE_BITS[channel];
                setUpdateEventBits(sensorUpdateBit);
                
                MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, DATA_READY_BIT);
                notifyDataReceiver();
                
                LOG_MB8ART_DEBUG_NL("Set update bit 0x%08X for sensor %d after config write", 
                                 sensorUpdateBit, channel + 1);
            } else if (startingAddress == MEASUREMENT_RANGE_REGISTER) {
                // Update our local copy if we have the echoed value
                if (length >= 4) {
                    uint16_t echoedValue = (data[2] << 8) | data[3];
                    currentRange = static_cast<mb8art::MeasurementRange>(echoedValue);
                    LOG_MB8ART_INFO_NL("Measurement range write acknowledged: %s",
                                      (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
                } else {
                    LOG_MB8ART_DEBUG_NL("Measurement range write acknowledged");
                }
            }
            break;
        }

        default:
            LOG_MB8ART_WARN_NL("Unhandled Function Code: %d", fc);
            break;
    }
}




// Spinlock for thread-safe access to error log throttle array
static portMUX_TYPE errorLogThrottleMutex = portMUX_INITIALIZER_UNLOCKED;

// Implementation of handleSensorError with char buffer
void MB8ART::handleSensorError(int sensorIndex, char* statusBuffer, size_t bufferSize, int& offset) {
    sensorReadings[sensorIndex].isTemperatureValid = false;
    sensorReadings[sensorIndex].Error = true;

    // Mark sensor as disconnected on error
    setSensorConnected(sensorIndex, false);

    // Append error information to the buffer
    int remaining = bufferSize - offset - 1;  // -1 for null terminator
    if (remaining > 0) {
        int written = snprintf(statusBuffer + offset, remaining, "C%d: Error; ", sensorIndex);
        if (written > 0 && written < remaining) {
            offset += written;
        }
    }

    // Throttle error logging to once per 30 seconds per channel to prevent log flooding
    // Thread-safe access to static throttle array
    static uint32_t lastErrorLogTime[DEFAULT_NUMBER_OF_SENSORS] = {0};
    uint32_t now = xTaskGetTickCount();

    taskENTER_CRITICAL(&errorLogThrottleMutex);
    bool shouldLog = (now - lastErrorLogTime[sensorIndex] > pdMS_TO_TICKS(30000));
    if (shouldLog) {
        lastErrorLogTime[sensorIndex] = now;
    }
    taskEXIT_CRITICAL(&errorLogThrottleMutex);

    if (shouldLog) {
        LOG_MB8ART_ERROR_NL("Sensor %d: Error encountered", sensorIndex);
    }
}




bool MB8ART::validateChannelConfig(uint8_t channelMode, uint8_t subType) {
    if (channelMode > static_cast<uint8_t>(mb8art::ChannelMode::CURRENT)) {
        LOG_MB8ART_ERROR_NL("Invalid channel mode: 0x%02X", channelMode);
        return false;
    }

    static const std::unordered_map<mb8art::ChannelMode, uint8_t> maxSubTypes = {
        {mb8art::ChannelMode::THERMOCOUPLE, static_cast<uint8_t>(mb8art::ThermocoupleType::TYPE_N)},
        {mb8art::ChannelMode::PT_INPUT, static_cast<uint8_t>(mb8art::PTType::CU100)},
        {mb8art::ChannelMode::VOLTAGE, static_cast<uint8_t>(mb8art::VoltageRange::V_1)},
        {mb8art::ChannelMode::CURRENT, static_cast<uint8_t>(mb8art::CurrentRange::MA_4_TO_20)},
        {mb8art::ChannelMode::DEACTIVATED, 0}
    };

    auto mode = static_cast<mb8art::ChannelMode>(channelMode);
    auto it = maxSubTypes.find(mode);
    if (it != maxSubTypes.end() && subType <= it->second) {
        return true;
    }

    LOG_MB8ART_ERROR_NL("Invalid subtype 0x%02X for mode %s", 
                        subType, 
                        mb8art::channelModeToString(mode));
    return false;
}




bool MB8ART::hasModbusResponseCallback() const {
    return static_cast<bool>(modbusResponseCallback);
}




void MB8ART::invokeModbusResponseCallback(uint8_t functionCode, const uint8_t* data, uint16_t length) const {
 
    LOG_MB8ART_DEBUG_NL("Inside MB8ART::invokeModbusResponseCallback...");
    LOG_MB8ART_DEBUG_NL("modbusResponseCallback is set: %s", modbusResponseCallback ? "Yes" : "No");

    if (modbusResponseCallback) {
        modbusResponseCallback(functionCode, data, length);
    }
}




bool MB8ART::validatePacketLength(size_t receivedLength, size_t expectedLength, const char* context) {
    if (receivedLength != expectedLength) {
        LOG_MB8ART_ERROR_NL("Invalid packet length for %s: expected=%d, received=%d", 
                           context, expectedLength, receivedLength);
        return false;
    }
    return true;
}




void MB8ART::handleModbusError(ModbusError error) {
    // Record error with automatic categorization for diagnostics
    auto category = modbus::ModbusErrorTracker::categorizeError(error);
    modbus::ModbusErrorTracker::recordError(getServerAddress(), category);

    // Use the helper for consistent, descriptive error messages
    LOG_MB8ART_ERROR_NL("Modbus error: %s (0x%02X)",
                        getModbusErrorString(error),
                        static_cast<int>(error));

    // Device-specific handling for certain errors
    switch(error) {
        case ModbusError::TIMEOUT:
            // Additional timeout-specific logic
            LOG_MB8ART_ERROR_NL("Device may be offline - check power and connections");
            break;
        
        case ModbusError::CRC_ERROR:
            // Additional CRC error handling
            LOG_MB8ART_ERROR_NL("Check RS485 wiring and termination resistors");
            break;
            
        case ModbusError::ILLEGAL_DATA_ADDRESS:
            // Handle invalid register address
            LOG_MB8ART_ERROR_NL("Invalid register address - check device documentation");
            break;
            
        case ModbusError::SLAVE_DEVICE_FAILURE:
            // Device reported internal error
            LOG_MB8ART_ERROR_NL("Device reported internal failure - may need reset");
            break;
            
        default:
            // Other errors already have descriptions from getModbusErrorString()
            break;
    }
    
    // Set error bits for all sensors (interleaved format)
    setErrorEventBits(mb8art::ALL_SENSOR_ERROR_BITS);
}





void MB8ART::handleConnectionStatus(const uint8_t* data, size_t length) {
    LOG_MB8ART_DEBUG_NL("handleConnectionStatus called with length=%d", length);
    
    if (!data) {
        LOG_MB8ART_ERROR_NL("Data pointer is NULL");
        return;
    }

    EventBits_t errorBitsToSet = 0;
    EventBits_t errorBitsToClear = 0;

    // Process all 8 channels
    for (size_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; ++i) {
        // Discrete inputs: 0 = disconnected, 1 = connected
        // Unpack the bit for sensor 'i'
        uint8_t byte_index = i / 8;
        uint8_t bit_index_in_byte = i % 8;
        bool connected = (data[byte_index] >> bit_index_in_byte) & 0x01;
        
        updateConnectionStatus(i, connected);
        
        // Only mark as error if channel is active and disconnected
        if (!connected && channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            errorBitsToSet |= mb8art::SENSOR_ERROR_BITS[i];
            // Don't invalidate temperature data here - let the temperature reading handle that
        } else if (connected) {
            errorBitsToClear |= mb8art::SENSOR_ERROR_BITS[i];
        }
    }

    if (errorBitsToSet) {
        setErrorEventBits(errorBitsToSet);
    }
    if (errorBitsToClear) {
        clearErrorEventBits(errorBitsToClear);
    }
    
    // Update cache timestamp since we just received fresh connection status
    lastConnectionStatusCheck = xTaskGetTickCount();
}




void MB8ART::handleDisconnection() {
    LOG_MB8ART_ERROR_NL("Device connection lost: %d", getServerAddress());
    setErrorEventBits(mb8art::ALL_SENSOR_ERROR_BITS);
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        sensorReadings[i].isTemperatureValid = false;
        sensorReadings[i].Error = true;
    }
}


