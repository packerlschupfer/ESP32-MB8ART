#ifndef MOCK_MB8ART_H
#define MOCK_MB8ART_H

#include <vector>
#include <map>
#include <functional>
#include <cmath>
#include "MB8ART.h"

/**
 * @class MockMB8ART
 * @brief Mock implementation of MB8ART for unit testing
 * 
 * This mock class simulates MB8ART behavior without requiring actual hardware.
 * It allows testing of:
 * - Initialization sequences
 * - Temperature data simulation
 * - Error conditions
 * - Configuration changes
 * - Modbus communication patterns
 */
class MockMB8ART : public MB8ART {
public:
    /**
     * @brief Constructor
     * @param address Simulated Modbus address
     */
    explicit MockMB8ART(uint8_t address) : MB8ART(address, "MockMB8ART") {
        // Initialize mock data
        initializeMockData();
    }
    
    /**
     * @brief Configure mock temperature values
     * @param channel Channel index (0-7)
     * @param temperature Temperature value to simulate
     * @param connected Whether sensor is connected
     */
    void setMockTemperature(uint8_t channel, float temperature, bool connected = true) {
        if (channel < DEFAULT_NUMBER_OF_SENSORS) {
            mockTemperatures[channel] = temperature;
            mockSensorConnected[channel] = connected;
        }
    }
    
    /**
     * @brief Configure mock measurement range
     * @param range Measurement range to simulate
     */
    void setMockMeasurementRange(mb8art::MeasurementRange range) {
        mockRange = range;
    }
    
    /**
     * @brief Configure mock channel configuration
     * @param channel Channel index
     * @param mode Channel mode
     * @param subType Channel subtype
     */
    void setMockChannelConfig(uint8_t channel, mb8art::ChannelMode mode, uint16_t subType) {
        if (channel < DEFAULT_NUMBER_OF_SENSORS) {
            mockChannelConfigs[channel] = {static_cast<uint16_t>(mode), subType};
            // Also update parent's protected channelConfigs for updateActiveChannelMask()
            channelConfigs[channel] = {static_cast<uint16_t>(mode), subType};
        }
    }
    
    /**
     * @brief Simulate initialization failure
     * @param fail Whether initialization should fail
     */
    void setInitializationFailure(bool fail) {
        shouldFailInit = fail;
    }
    
    /**
     * @brief Simulate device offline state
     * @param offline Whether device should appear offline
     */
    void setDeviceOffline(bool offline) {
        mockOffline = offline;
    }
    
    /**
     * @brief Get number of temperature requests made
     * @return Request count
     */
    uint32_t getTemperatureRequestCount() const {
        return temperatureRequestCount;
    }
    
    /**
     * @brief Reset all counters
     */
    void resetCounters() {
        temperatureRequestCount = 0;
        configRequestCount = 0;
    }
    
    /**
     * @brief Trigger a simulated Modbus response
     * @param functionCode Function code
     * @param address Register address
     * @param data Response data
     * @param length Data length
     */
    void simulateModbusResponse(uint8_t functionCode, uint16_t address, 
                               const uint8_t* data, size_t length) {
        handleModbusResponse(functionCode, address, data, length);
    }
    
    /**
     * @brief Simulate error response
     * @param error Error type to simulate
     */
    void simulateError(ModbusError error) {
        handleModbusError(error);
        lastError = error;
        errorStats[error]++;
    }
    
    /**
     * @brief Get error statistics
     * @return Map of error types to counts
     */
    std::map<ModbusError, uint32_t> getErrorStatistics() const {
        return errorStats;
    }
    
    /**
     * @brief Get specific error count
     * @param error Error type to check
     * @return Number of times this error occurred
     */
    uint32_t getErrorCount(ModbusError error) const {
        auto it = errorStats.find(error);
        return (it != errorStats.end()) ? it->second : 0;
    }
    
    /**
     * @brief Reset error statistics
     */
    void resetErrorStatistics() {
        errorStats.clear();
        lastError = ModbusError::SUCCESS;
    }

    // ========================================================================
    // Issue Fix Test Helpers (Added for test_mb8art_issue_fixes.cpp)
    // ========================================================================
    // Note: getActiveChannelMask(), getActiveChannelCount(), and
    // getConsecutiveTimeouts() are inherited from MB8ART's public interface

    /**
     * @brief Simulate a timeout in waitForData
     * Increments consecutiveTimeouts and potentially sets offline flag
     */
    void simulateTimeout() {
        incrementTimeoutCounter();  // Use protected method from MB8ART
    }

    /**
     * @brief Simulate a successful Modbus response
     * Resets consecutiveTimeouts and clears offline flag
     */
    void simulateSuccessfulResponse() {
        resetTimeoutCounter();  // Use protected method from MB8ART
    }

    /**
     * @brief Check if device is ready (initialized)
     * @return true if initialized successfully
     */
    bool isReady() const {
        return mockInitialized;
    }

    /**
     * @brief Force update of active channel mask
     * Wrapper to call protected parent method
     */
    void forceUpdateActiveChannelMask() {
        updateActiveChannelMask();  // Protected method from MB8ART
    }

    /**
     * @brief Initialize the mock device
     * @return DeviceResult indicating success or failure
     */
    IDeviceInstance::DeviceResult<void> initialize() {
        if (shouldFailInit) {
            return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
        }

        // Copy mock channel configs to parent's protected member
        currentRange = mockRange;
        for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            channelConfigs[i] = mockChannelConfigs[i];
        }

        mockInitialized = true;
        updateActiveChannelMask();

        return IDeviceInstance::DeviceResult<void>();
    }

private:
    // Mock data storage
    std::array<float, DEFAULT_NUMBER_OF_SENSORS> mockTemperatures;
    std::array<bool, DEFAULT_NUMBER_OF_SENSORS> mockSensorConnected;
    std::array<mb8art::ChannelConfig, DEFAULT_NUMBER_OF_SENSORS> mockChannelConfigs;
    mb8art::MeasurementRange mockRange;
    
    // Mock behavior flags
    bool shouldFailInit = false;
    bool mockOffline = false;
    bool mockInitialized = false;
    
    // Tracking counters
    uint32_t temperatureRequestCount = 0;
    uint32_t configRequestCount = 0;
    
    // Error tracking
    ModbusError lastError = ModbusError::SUCCESS;
    std::map<ModbusError, uint32_t> errorStats;
    
    /**
     * @brief Initialize mock data with defaults
     */
    void initializeMockData() {
        mockRange = mb8art::MeasurementRange::LOW_RES;
        
        for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            mockTemperatures[i] = 20.0f + i;  // Default temps: 20, 21, 22...
            mockSensorConnected[i] = true;
            mockChannelConfigs[i] = {
                static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT),
                static_cast<uint16_t>(mb8art::PTType::PT1000)
            };
        }
    }
};

#endif // MOCK_MB8ART_H