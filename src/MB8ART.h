#ifndef MB8ART_H
#define MB8ART_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <array>
#include <functional>
#include "QueuedModbusDevice.h"
#include "ModbusTypes.h"
#include <MutexGuard.h>
#include <IDeviceInstance.h>
#include "CommonModbusDefinitions.h"
#include "MB8ARTLoggingMacros.h"
#include "MB8ARTSharedResources.h"

// Import ModbusDevice types into global scope for MB8ART usage
using modbus::ModbusResult;
using modbus::ModbusError;
using modbus::QueuedModbusDevice;

// Forward declarations and constants
#define DEFAULT_NUMBER_OF_SENSORS 8

// Timing constants for MB8ART operation
#ifndef MB8ART_MIN_REQUEST_INTERVAL_MS
    #ifdef PROJECT_MB8ART_MIN_REQUEST_INTERVAL_MS
        #define MB8ART_MIN_REQUEST_INTERVAL_MS PROJECT_MB8ART_MIN_REQUEST_INTERVAL_MS
    #else
        #define MB8ART_MIN_REQUEST_INTERVAL_MS 25    // Default 25ms
    #endif
#endif

#ifndef MB8ART_REQUEST_TIMEOUT_MS
    #ifdef PROJECT_MB8ART_REQUEST_TIMEOUT_MS
        #define MB8ART_REQUEST_TIMEOUT_MS PROJECT_MB8ART_REQUEST_TIMEOUT_MS
    #else
        #define MB8ART_REQUEST_TIMEOUT_MS 500        // Default 500ms timeout for Modbus requests
    #endif
#endif

#ifndef MB8ART_INTER_REQUEST_DELAY_MS
    #ifdef PROJECT_MB8ART_INTER_REQUEST_DELAY_MS
        #define MB8ART_INTER_REQUEST_DELAY_MS PROJECT_MB8ART_INTER_REQUEST_DELAY_MS
    #else
        #define MB8ART_INTER_REQUEST_DELAY_MS 5     // Default 5ms delay between consecutive requests
    #endif
#endif

#ifndef MB8ART_RETRY_COUNT
    #ifdef PROJECT_MB8ART_RETRY_COUNT
        #define MB8ART_RETRY_COUNT PROJECT_MB8ART_RETRY_COUNT
    #else
        #define MB8ART_RETRY_COUNT 3                 // Default 3 retries for failed requests
    #endif
#endif

// Async queue size - sufficient for 2-3 typical requests + 5 burst during init
#ifndef MB8ART_ASYNC_QUEUE_SIZE
    #ifdef PROJECT_MB8ART_ASYNC_QUEUE_SIZE
        #define MB8ART_ASYNC_QUEUE_SIZE PROJECT_MB8ART_ASYNC_QUEUE_SIZE
    #else
        #define MB8ART_ASYNC_QUEUE_SIZE 15           // Default 15 slots for better reliability
    #endif
#endif

namespace mb8art {

// =============================================================================
// Interleaved Event Bits (RYN4 pattern - single event group for all sensors)
// =============================================================================
// Layout: U0 E0 U1 E1 U2 E2 U3 E3 U4 E4 U5 E5 U6 E6 U7 E7
// Where U=Update bit, E=Error bit, number=sensor index (0-7)
// This saves one event group (~70 bytes) and allows atomic per-sensor checks

// Update bits (even positions: 0, 2, 4, 6, 8, 10, 12, 14)
static constexpr uint32_t SENSOR0_UPDATE_BIT = (1UL << 0UL);
static constexpr uint32_t SENSOR1_UPDATE_BIT = (1UL << 2UL);
static constexpr uint32_t SENSOR2_UPDATE_BIT = (1UL << 4UL);
static constexpr uint32_t SENSOR3_UPDATE_BIT = (1UL << 6UL);
static constexpr uint32_t SENSOR4_UPDATE_BIT = (1UL << 8UL);
static constexpr uint32_t SENSOR5_UPDATE_BIT = (1UL << 10UL);
static constexpr uint32_t SENSOR6_UPDATE_BIT = (1UL << 12UL);
static constexpr uint32_t SENSOR7_UPDATE_BIT = (1UL << 14UL);

// Error bits (odd positions: 1, 3, 5, 7, 9, 11, 13, 15)
static constexpr uint32_t SENSOR0_ERROR_BIT = (1UL << 1UL);
static constexpr uint32_t SENSOR1_ERROR_BIT = (1UL << 3UL);
static constexpr uint32_t SENSOR2_ERROR_BIT = (1UL << 5UL);
static constexpr uint32_t SENSOR3_ERROR_BIT = (1UL << 7UL);
static constexpr uint32_t SENSOR4_ERROR_BIT = (1UL << 9UL);
static constexpr uint32_t SENSOR5_ERROR_BIT = (1UL << 11UL);
static constexpr uint32_t SENSOR6_ERROR_BIT = (1UL << 13UL);
static constexpr uint32_t SENSOR7_ERROR_BIT = (1UL << 15UL);

// Helper arrays for indexed access (constexpr - lives in flash)
static constexpr uint32_t SENSOR_UPDATE_BITS[8] = {
    SENSOR0_UPDATE_BIT, SENSOR1_UPDATE_BIT, SENSOR2_UPDATE_BIT, SENSOR3_UPDATE_BIT,
    SENSOR4_UPDATE_BIT, SENSOR5_UPDATE_BIT, SENSOR6_UPDATE_BIT, SENSOR7_UPDATE_BIT
};

static constexpr uint32_t SENSOR_ERROR_BITS[8] = {
    SENSOR0_ERROR_BIT, SENSOR1_ERROR_BIT, SENSOR2_ERROR_BIT, SENSOR3_ERROR_BIT,
    SENSOR4_ERROR_BIT, SENSOR5_ERROR_BIT, SENSOR6_ERROR_BIT, SENSOR7_ERROR_BIT
};

// Combined masks for all sensors
static constexpr uint32_t ALL_SENSOR_UPDATE_BITS =
    SENSOR0_UPDATE_BIT | SENSOR1_UPDATE_BIT | SENSOR2_UPDATE_BIT | SENSOR3_UPDATE_BIT |
    SENSOR4_UPDATE_BIT | SENSOR5_UPDATE_BIT | SENSOR6_UPDATE_BIT | SENSOR7_UPDATE_BIT;

static constexpr uint32_t ALL_SENSOR_ERROR_BITS =
    SENSOR0_ERROR_BIT | SENSOR1_ERROR_BIT | SENSOR2_ERROR_BIT | SENSOR3_ERROR_BIT |
    SENSOR4_ERROR_BIT | SENSOR5_ERROR_BIT | SENSOR6_ERROR_BIT | SENSOR7_ERROR_BIT;

// Channel and sensor type enums
enum class ChannelMode : uint16_t {
    DEACTIVATED = 0x00,
    THERMOCOUPLE = 0x01,
    PT_INPUT = 0x02,
    VOLTAGE = 0x03,
    CURRENT = 0x04,
};

// Define sub-types for Thermocouple, PT, Voltage, and Current
enum class ThermocoupleType : uint16_t {
    TYPE_J = 0x00,
    TYPE_K = 0x01,
    TYPE_T = 0x02,
    TYPE_E = 0x03,
    TYPE_R = 0x04,
    TYPE_S = 0x05,
    TYPE_B = 0x06,
    TYPE_N = 0x07
};

enum class PTType : uint16_t {
    PT100 = 0x00,
    PT1000 = 0x01,
    CU50 = 0x02,
    CU100 = 0x03
};

enum class VoltageRange : uint16_t {
    MV_15 = 0x00,
    MV_50 = 0x01,
    MV_100 = 0x02,
    V_1 = 0x03
};

enum class CurrentRange : uint16_t {
    MA_20 = 0x00,
    MA_4_TO_20 = 0x01
};

// enum for measurement range configuration
enum class MeasurementRange {
    LOW_RES = 0,  // -200 to 850°C, 0.1° resolution
    HIGH_RES = 1  // -200 to 200°C, 0.01° resolution
};

// Migration complete - now using IDeviceInstance types directly
using DeviceError = IDeviceInstance::DeviceError;
template<typename T>
using DeviceResult = IDeviceInstance::DeviceResult<T>;

// Struct for sensor readings - enhanced with tracking info and memory optimized
struct SensorReading {
    int16_t temperature;  // Temperature in tenths of degrees (Temperature_t format: 244 = 24.4°C)
    TickType_t lastTemperatureUpdated;

    // Bit field flags to save memory (4 bools -> 1 byte)
    uint8_t isTemperatureValid : 1;
    uint8_t Error : 1;
    uint8_t lastCommandSuccess : 1;  // Track if last command succeeded
    uint8_t isStateConfirmed : 1;    // Track if state has been confirmed
    uint8_t reserved : 4;            // Reserved for future use

    // Constructor for initialization
    SensorReading() :
        temperature(0),
        lastTemperatureUpdated(0),
        isTemperatureValid(0),
        Error(0),
        lastCommandSuccess(0),
        isStateConfirmed(0),
        reserved(0) {}
};

struct ChannelConfig {
    uint16_t mode;    // Channel mode (e.g., THERMOCOUPLE, PT_INPUT, etc.)
    uint16_t subType; // Subtype (e.g., J-Type, PT100, ±15mV, etc.)
};

/**
 * @brief Hardware configuration for a single sensor channel (constexpr - lives in flash)
 *
 * This struct contains all the static hardware information about a sensor
 * that never changes at runtime. It should be instantiated as constexpr
 * arrays in flash memory to save RAM.
 */
struct SensorHardwareConfig {
    uint8_t channelNumber;     // Physical channel number (0-7)
    EventBits_t updateEventBit; // Event bit for successful update
    EventBits_t errorEventBit;  // Event bit for error indication
    bool isActive;              // Sensor channel is in use
};

/**
 * @brief Runtime binding for a single sensor channel (lives in RAM)
 *
 * This struct contains only the runtime pointers to the application's
 * temperature and validity variables. It's initialized once at startup.
 */
struct SensorBinding {
    int16_t* temperaturePtr;   // Pointer to temperature in tenths of degrees (Temperature_t)
    bool* validityPtr;         // Pointer to validity flag in application
};

/**
 * @brief Default hardware configuration for all 8 sensor channels
 *
 * This constexpr array lives in flash memory and provides the default
 * hardware configuration for a standard MB8ART sensor module. Applications
 * can reference this or create their own constexpr config.
 */
static constexpr std::array<SensorHardwareConfig, 8> DEFAULT_SENSOR_CONFIG = {{
    {0, SENSOR_UPDATE_BITS[0], SENSOR_ERROR_BITS[0], true},
    {1, SENSOR_UPDATE_BITS[1], SENSOR_ERROR_BITS[1], true},
    {2, SENSOR_UPDATE_BITS[2], SENSOR_ERROR_BITS[2], true},
    {3, SENSOR_UPDATE_BITS[3], SENSOR_ERROR_BITS[3], true},
    {4, SENSOR_UPDATE_BITS[4], SENSOR_ERROR_BITS[4], true},
    {5, SENSOR_UPDATE_BITS[5], SENSOR_ERROR_BITS[5], true},
    {6, SENSOR_UPDATE_BITS[6], SENSOR_ERROR_BITS[6], true},
    {7, SENSOR_UPDATE_BITS[7], SENSOR_ERROR_BITS[7], true}
}};

// String conversion functions
inline const char* channelModeToString(ChannelMode mode) {
    switch (mode) {
        case ChannelMode::THERMOCOUPLE: return "THERMOCOUPLE";
        case ChannelMode::PT_INPUT: return "PT_INPUT";
        case ChannelMode::VOLTAGE: return "VOLTAGE";
        case ChannelMode::CURRENT: return "CURRENT";
        case ChannelMode::DEACTIVATED: return "DEACTIVATED";
        default: return "UNKNOWN";
    }
}

inline const char* thermocoupleTypeToString(ThermocoupleType type) {
    switch (type) {
        case ThermocoupleType::TYPE_J: return "TYPE_J";
        case ThermocoupleType::TYPE_K: return "TYPE_K";
        case ThermocoupleType::TYPE_T: return "TYPE_T";
        case ThermocoupleType::TYPE_E: return "TYPE_E";
        case ThermocoupleType::TYPE_R: return "TYPE_R";
        case ThermocoupleType::TYPE_S: return "TYPE_S";
        case ThermocoupleType::TYPE_B: return "TYPE_B";
        case ThermocoupleType::TYPE_N: return "TYPE_N";
        default: return "UNKNOWN_THERMOCOUPLE_TYPE";
    }
}

inline const char* ptTypeToString(PTType type) {
    switch (type) {
        case PTType::PT100: return "PT100";
        case PTType::PT1000: return "PT1000";
        case PTType::CU50: return "CU50";
        case PTType::CU100: return "CU100";
        default: return "UNKNOWN_PT_TYPE";
    }
}

inline const char* voltageRangeToString(VoltageRange range) {
    switch (range) {
        case VoltageRange::MV_15: return "±15mV";
        case VoltageRange::MV_50: return "±50mV";
        case VoltageRange::MV_100: return "±100mV";
        case VoltageRange::V_1: return "±1V";
        default: return "UNKNOWN_VOLTAGE_RANGE";
    }
}

inline const char* currentRangeToString(CurrentRange range) {
    switch (range) {
        case CurrentRange::MA_20: return "±20mA";
        case CurrentRange::MA_4_TO_20: return "4-20mA";
        default: return "UNKNOWN_CURRENT_RANGE";
    }
}

} // namespace mb8art

class MB8ART : public QueuedModbusDevice, public IDeviceInstance {
public:
    using SensorReading = mb8art::SensorReading;

    // Inter-task communication event bits (for main.cpp tasks)
    enum TaskEventBits {
        DATA_REQUEST_BIT = (1 << 0),     // Control task has made a request
        DATA_READY_BIT = (1 << 1),       // Data is ready for processing
        DATA_ERROR_BIT = (1 << 2),       // Error occurred during request
        REQUEST_PENDING_BIT = (1 << 3),  // Request is in progress
        INIT_COMPLETE_BIT = (1 << 4)     // Device initialization complete
    };

    // Internal initialization tracking bits
    struct InitBits {
        static constexpr uint32_t MEASUREMENT_RANGE = (1 << 0);
        static constexpr uint32_t CHANNEL_CONFIG = (1 << 1);
        static constexpr uint32_t DEVICE_RESPONSIVE = (1 << 2);
        static constexpr uint32_t ALL_BITS = MEASUREMENT_RANGE | CHANNEL_CONFIG | DEVICE_RESPONSIVE;
    };

    explicit MB8ART(uint8_t sensorAddress, const char* tag = "MB8ART");
    ~MB8ART() override; // Ensure correct overriding

    // Core initialization and status methods
    bool initializeDevice();  // Custom initialization to enable async mode
    bool isReady() const { return getInitPhase() == InitPhase::READY; }
    bool isInitialized() const noexcept override { return statusFlags.initialized; }
    bool isModuleResponsive() const;
    bool batchReadAllConfig();
    bool batchReadInitialConfig();

    /**
     * @brief Bind sensor data pointers (unified mapping API)
     *
     * This method accepts an array of pointers to application temperature and validity variables.
     * When sensor readings are updated, the library will update these variables directly.
     *
     * @param bindings Array of 8 SensorBinding structs (temperaturePtr and validityPtr)
     *
     * Example usage:
     * @code
     * std::array<mb8art::SensorBinding, 8> bindings = {{
     *     {&sensorData.boilerOutput, &sensorData.isBoilerOutputValid},
     *     {&sensorData.boilerReturn, &sensorData.isBoilerReturnValid},
     *     ...
     * }};
     * mb8art->bindSensorPointers(bindings);
     * @endcode
     */
    void bindSensorPointers(const std::array<mb8art::SensorBinding, 8>& bindings);

    /**
     * @brief Set hardware configuration (unified mapping API)
     *
     * This method accepts a pointer to a constexpr hardware config array.
     * The config defines the event bits and channel numbers.
     *
     * @param config Pointer to array of 8 SensorHardwareConfig structs
     *
     * Example usage:
     * @code
     * mb8art->setHardwareConfig(mb8art::DEFAULT_SENSOR_CONFIG.data());
     * @endcode
     */
    void setHardwareConfig(const mb8art::SensorHardwareConfig* config);

    // MB8ART configuration methods
    bool configure();
    float getScaleFactor(size_t channel = 0) const;
    
    // QueuedModbusDevice overrides
    void onAsyncResponse(uint8_t functionCode, uint16_t address,
                        const uint8_t* data, size_t length) override;
    
    // Additional MB8ART-specific overrides
    void handleModbusResponse(uint8_t functionCode, uint16_t address, 
                             const uint8_t* data, size_t length) override;
    void handleModbusError(ModbusError error) override;
    void invokeModbusResponseCallback(uint8_t functionCode, const uint8_t* data, uint16_t length) const;
    
    // MB8ART specific data methods
    bool requestTemperatures();
    bool waitForData() override;
    IDeviceInstance::DeviceError waitForData(TickType_t xTicksToWait) override;
    std::vector<int16_t> getTemperatures() const;
    int16_t getTemperature(uint8_t channel) const;
    
    // Event group access - returns the task communication event group
    EventGroupHandle_t getEventGroup() const noexcept override { return xTaskEventGroup; }

    // Get the sensor event group (interleaved update/error bits)
    EventGroupHandle_t getSensorEventGroup() const noexcept { return xSensorEventGroup; }

    // Task notification setup
    void setDataReceiverTask(TaskHandle_t handle);
    TaskHandle_t getDataReceiverTask() const { return dataReceiverTask; }

    // Configuration methods
    IDeviceInstance::DeviceResult<void> configureMeasurementRange(mb8art::MeasurementRange range);
    IDeviceInstance::DeviceResult<void> configureChannelMode(uint8_t channel, uint16_t mode);

    // Essential configuration requests
    bool reqAllChannelModes();
    bool reqChannelMode(uint8_t channel);
    bool reqMeasurementRange();
    IDeviceInstance::DeviceResult<void> reqTemperatures(int numberOfSensors = DEFAULT_NUMBER_OF_SENSORS, bool highResolution = false);
    bool reqModuleTemperature();
    bool reqAddress();
    bool reqBaudRate();
    bool reqParity();

    bool setFactoryReset();
    bool setAddress(uint8_t addressValue);
    bool setBaudRate(uint8_t baudRateValue);
    bool setParity(uint8_t parity);

    // Batch configuration methods
    IDeviceInstance::DeviceResult<void> configureAllChannels(mb8art::ChannelMode mode, uint16_t subType);
    IDeviceInstance::DeviceResult<void> configureChannelRange(uint8_t startChannel, uint8_t endChannel, 
                              mb8art::ChannelMode mode, uint16_t subType);
    
    // Enhanced data acquisition
    IDeviceInstance::DeviceResult<void> requestAllData();  // Request temperature + connection status + module info
    bool refreshConnectionStatus(); // Request connection status only

    // Diagnostic methods
    void printChannelDiagnostics();
    // Note: getActiveChannelCount() defined inline below with other test accessors
    uint8_t getConnectedChannels() const;

    // State and status methods
    bool hasModbusResponseCallback() const;
    const SensorReading& getSensorReading(uint8_t index) const;
    bool getAllSensorReadings(SensorReading* destination) const;
    bool getSensorConnectionStatus(uint8_t channel) const;
    
    // Enhanced state tracking methods
    int16_t getSensorTemperature(uint8_t sensorIndex) const;
    bool wasSensorLastCommandSuccessful(uint8_t sensorIndex) const;
    TickType_t getSensorLastUpdateTime(uint8_t sensorIndex) const;

    bool isSensorStateConfirmed(uint8_t sensorIndex) const;

    // Event bit handling methods
    void setUpdateEventBits(uint32_t bitsToSet);
    void clearUpdateEventBits(uint32_t bitsToClear);
    void setErrorEventBits(uint32_t bitsToSet);
    void clearErrorEventBits(uint32_t bitsToClear);
    void updateSensorEventBits(uint8_t sensorIndex, bool isValid, bool hasError);

    // Clear all sensor update/error bits (uses interleaved event group)
    void clearAllUpdateBits() {
        xEventGroupClearBits(xSensorEventGroup, mb8art::ALL_SENSOR_UPDATE_BITS);
    }

    void clearAllErrorBits() {
        xEventGroupClearBits(xSensorEventGroup, mb8art::ALL_SENSOR_ERROR_BITS);
    }

    void clearAllSensorBits() {
        xEventGroupClearBits(xSensorEventGroup, mb8art::ALL_SENSOR_UPDATE_BITS | mb8art::ALL_SENSOR_ERROR_BITS);
    }

    // Check if any sensor has an update pending
    bool hasAnyUpdatePending() const {
        EventBits_t bits = xEventGroupGetBits(xSensorEventGroup);
        return (bits & mb8art::ALL_SENSOR_UPDATE_BITS) != 0;
    }

    // Check if any sensor has an error
    bool hasAnyError() const {
        EventBits_t bits = xEventGroupGetBits(xSensorEventGroup);
        return (bits & mb8art::ALL_SENSOR_ERROR_BITS) != 0;
    }
    
    // Check if module is offline/unresponsive
    bool isModuleOffline() const { return statusFlags.moduleOffline; }

    // Test accessors for consecutive timeout tracking
    uint8_t getConsecutiveTimeouts() const { return consecutiveTimeouts; }
    static constexpr uint8_t getOfflineThreshold() { return OFFLINE_THRESHOLD; }

    // Test accessors for active channel optimization
    EventBits_t getActiveChannelMask() const { return activeChannelMask; }
    uint8_t getActiveChannelCount() const { return activeChannelCount; }

    // Probe device to check if it's responsive
    bool probeDevice();

    // Utility methods
    const char* getTag() const;
    const char* getSubTypeString(mb8art::ChannelMode mode, uint8_t subType) const;
    void setTag(const char* newTag);
    void printSensorReading(const SensorReading& reading, int sensorIndex);
    void printModuleSettings() const;

    const ModuleSettings& getModuleSettings() const { return moduleSettings; }
    const SensorReading* getSensorReadings() const { return sensorReadings; }
    const mb8art::ChannelConfig* getChannelConfigs() const { return channelConfigs; }
    mb8art::MeasurementRange getCurrentRange() const { return currentRange; }

    // Optional RS485 settings accessors
    BaudRate getStoredBaudRate() const;
    Parity getStoredParity() const;

    // Callback registration
    void registerModbusResponseCallback(std::function<void(uint8_t functionCode, const uint8_t* data, uint16_t length)> callback);

    // Static utilities
    static BaudRate getBaudRateEnum(uint8_t rawValue);
    static Parity getParityEnum(uint8_t rawValue);
    static std::string baudRateToString(BaudRate rate);
    static std::string parityToString(Parity parity);

    // Timeouts and constants
    static constexpr TickType_t mutexTimeout = pdMS_TO_TICKS(5000);

    // Set the expected update interval (should be called by the application)
    static void setExpectedUpdateInterval(TickType_t intervalMs) {
        expectedUpdateIntervalMs = intervalMs;
    }
    
    // Get the expected data update interval
    static TickType_t getExpectedUpdateInterval() {
        // Default to 2 seconds if not set
        return pdMS_TO_TICKS(expectedUpdateIntervalMs > 0 ? expectedUpdateIntervalMs : 2000);
    }
    
    // Check if any sensor has received data within the specified timeout
    // If timeoutMs is 0, no timeout check is performed
    bool hasRecentSensorData(TickType_t timeoutMs = 0) const;
    
    // Get timeout based on context
    static TickType_t getResponsivenessTimeout() {
        return getExpectedUpdateInterval() * RESPONSIVENESS_CHECK_MULTIPLIER;
    }
    
    static TickType_t getMonitoringTimeout() {
        return getExpectedUpdateInterval() * MONITORING_CHECK_MULTIPLIER;
    }

    // IDeviceInstance interface implementation
    IDeviceInstance::DeviceResult<void> initialize() override;
    void waitForInitialization() override;
    IDeviceInstance::DeviceResult<void> waitForInitializationComplete(TickType_t timeout = portMAX_DELAY) override;
    IDeviceInstance::DeviceResult<void> requestData() override;
    SemaphoreHandle_t getMutexInstance() const noexcept override { return initMutex; }
    IDeviceInstance::DeviceResult<void> processData() override;
    IDeviceInstance::DeviceResult<std::vector<float>> getData(IDeviceInstance::DeviceDataType dataType) override;
    IDeviceInstance::DeviceResult<std::vector<int16_t>> getDataRaw(IDeviceInstance::DeviceDataType dataType) override;
    int16_t getDataScaleDivider(IDeviceInstance::DeviceDataType dataType) const override;
    int16_t getDataScaleDivider(IDeviceInstance::DeviceDataType dataType, uint8_t channel) const override;
    SemaphoreHandle_t getMutexInterface() const noexcept override { return interfaceMutex; }
    IDeviceInstance::DeviceResult<void> performAction(int actionId, int actionParam) override;
    
    // Event callbacks - not used by MB8ART but required by interface
    IDeviceInstance::DeviceResult<void> registerCallback(IDeviceInstance::EventCallback callback) override {
        (void)callback;
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }
    
    IDeviceInstance::DeviceResult<void> unregisterCallbacks() override {
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }
    
    IDeviceInstance::DeviceResult<void> setEventNotification(IDeviceInstance::EventType eventType, bool enable) override {
        (void)eventType;
        (void)enable;
        return IDeviceInstance::DeviceResult<void>(IDeviceInstance::DeviceError::UNKNOWN_ERROR);
    }

protected:
    // Protected interface for testing (MockMB8ART)
    // These methods allow mock objects to simulate hardware behavior

    /**
     * @brief Increment consecutive timeout counter and set offline if threshold reached
     * Called by waitForData() on timeout. Made protected for testing.
     */
    void incrementTimeoutCounter() {
        consecutiveTimeouts++;
        if (consecutiveTimeouts >= OFFLINE_THRESHOLD && !statusFlags.moduleOffline) {
            statusFlags.moduleOffline = 1;
        }
    }

    /**
     * @brief Reset timeout counter and clear offline flag
     * Called on successful Modbus response. Made protected for testing.
     */
    void resetTimeoutCounter() {
        consecutiveTimeouts = 0;
        statusFlags.moduleOffline = 0;
    }

    /**
     * @brief Update the pre-computed active channel mask
     * Call after channel configuration changes
     */
    void updateActiveChannelMask();

    // Protected access to channel configuration for mock initialization
    mb8art::ChannelConfig channelConfigs[DEFAULT_NUMBER_OF_SENSORS];
    mb8art::MeasurementRange currentRange = mb8art::MeasurementRange::LOW_RES;

private:
    // Private member variables
    const char* tag;
    
    // Bit field flags for memory optimization
    struct {
        uint8_t initialized : 1;
        uint8_t moduleOffline : 1;              // Track if module is offline/unresponsive
        uint8_t optionalSettingsPending : 1;    // Flag for deferred settings
        uint8_t reserved : 5;                   // Reserved for future flags
    } statusFlags;
    
    // Array of bit fields for sensor connection status (8 sensors = 1 byte)
    uint8_t sensorConnected;  // Each bit represents a sensor (0-7)

    // Unified mapping architecture
    const mb8art::SensorHardwareConfig* hardwareConfig; // Pointer to constexpr hardware config (flash)
    std::array<mb8art::SensorBinding, 8> sensorBindings; // Runtime sensor bindings (RAM)

    static TickType_t lastGlobalDataUpdate;
    uint8_t channelsConfiguredDuringInit;  // Bitmask to track configured channels during init
    IDeviceInstance::DeviceError lastError = IDeviceInstance::DeviceError::SUCCESS;  // Track last error for IDeviceInstance

    const char* channelModeToString(mb8art::ChannelMode mode);
    const char* thermocoupleTypeToString(mb8art::ThermocoupleType type);
    const char* ptTypeToString(mb8art::PTType type);
    const char* voltageRangeToString(mb8art::VoltageRange range);
    const char* currentRangeToString(mb8art::CurrentRange range);

    // Helper methods
    bool initializeModuleSettings();  // Returns false if device is offline
    void processModbusResponse(uint8_t functionCode, const uint8_t* data, uint16_t length);
    void notifyDataReceiver();

    // Data processing helpers
    void processTemperatureData(const uint8_t* data, size_t length,
                              EventBits_t& updateBitsToSet,
                              EventBits_t& errorBitsToSet,
                              EventBits_t& errorBitsToClear,
                              char* statusBuffer,
                              size_t bufferSize);
    int16_t processChannelData(uint8_t channel, uint16_t rawData);
    void updateSensorReading(uint8_t channel, int16_t value,
                           EventBits_t& updateBitsToSet,
                           EventBits_t& errorBitsToSet,
                           EventBits_t& errorBitsToClear,
                           char* statusBuffer,
                           size_t bufferSize,
                           int& offset);

    // Private helper methods
    bool validatePacketLength(size_t receivedLength, size_t expectedLength, const char* context);
    bool validateChannelConfig(uint8_t channelMode, uint8_t subType);
    void processChannelConfig(uint8_t channel, uint16_t rawConfig);
    void markChannelDeactivated(uint8_t channel, EventBits_t& errorBitsToSet, 
                               char* statusBuffer, size_t bufferSize, int& offset);
    bool isValueValid(float value) const;

    bool isSensorValid(uint8_t channel, float value) const;
    bool isTemperatureInRange(int16_t temperature);
    void handleSensorError(int sensorIndex, char* statusBuffer, size_t bufferSize, int& offset);
    void updateEventBits(EventBits_t updateBitsToSet, 
                        EventBits_t errorBitsToSet,
                        EventBits_t errorBitsToClear);
    bool checkAllInitBitsSet() const;
    bool waitForInitStep(EventBits_t stepBit, const char* stepName, TickType_t timeout = pdMS_TO_TICKS(5000));

    void clearDataEventBits();
    void clearPendingResponses();
    
    // Initialization helper methods
    void initializeDataStructures();
    bool registerWithModbusSystem();
    bool createSyncObjects();
    bool verifyCommunication();
    bool configureDevice();
    bool startDataAcquisition();
    bool performInitialDataRequest();
    uint8_t getSensorAddress() const { return getServerAddress(); }
    bool readRS485Settings();
    void cleanup();

    bool requestConnectionStatus();
    void handleConnectionStatus(const uint8_t* data, size_t length);
    void handleDisconnection();
    void updateConnectionStatus(uint8_t channel, bool connected);
    void readOptionalSettings();
    void setInitializationBit(EventBits_t bit);
    // Note: updateActiveChannelMask is protected for test access

    // Helper methods for bit field access
    inline bool isSensorConnected(uint8_t channel) const {
        return (channel < DEFAULT_NUMBER_OF_SENSORS) ? (sensorConnected & (1 << channel)) != 0 : false;
    }
    
    inline void setSensorConnected(uint8_t channel, bool connected) {
        if (channel < DEFAULT_NUMBER_OF_SENSORS) {
            if (connected) {
                sensorConnected |= (1 << channel);
            } else {
                sensorConnected &= ~(1 << channel);
            }
        }
    }

    // Process different sensor types
    int16_t processThermocoupleData(uint16_t rawData, mb8art::ThermocoupleType type);
    int16_t processPTData(uint16_t rawData, mb8art::PTType type, mb8art::MeasurementRange range);
    int16_t processVoltageData(uint16_t rawData, mb8art::VoltageRange range);   // Note: returns raw value for now
    int16_t processCurrentData(uint16_t rawData, mb8art::CurrentRange range);   // Note: returns raw value for now
    int16_t convertRawToTemperature(uint16_t rawData, bool highResolution);
    int16_t applyTemperatureCorrection(int16_t temperature);

    // Member variables for state tracking
    ModuleSettings moduleSettings;
    mb8art::SensorReading sensorReadings[DEFAULT_NUMBER_OF_SENSORS];
    // Note: channelConfigs and currentRange are protected for test access
    
    // Passive responsiveness tracking (from RYN4 suggestion)
    TickType_t lastResponseTime = 0;
    static constexpr TickType_t RESPONSIVE_TIMEOUT_MS = 30000; // 30 seconds
    
    // Optimization: Track when any channel was last updated
    TickType_t lastAnyChannelUpdate = 0;
    
    // Connection status caching
    TickType_t lastConnectionStatusCheck = 0;
    static constexpr TickType_t CONNECTION_STATUS_CACHE_MS = 5000; // Cache for 5 seconds
    
    // Pre-computed active channel mask for waitForData optimization
    EventBits_t activeChannelMask = 0;
    uint8_t activeChannelCount = 0;

    // Consecutive timeout tracking for automatic offline detection
    uint8_t consecutiveTimeouts = 0;
    static constexpr uint8_t OFFLINE_THRESHOLD = 3;  // Auto-offline after 3 consecutive timeouts

    // Event groups (RYN4 interleaved pattern)
    EventGroupHandle_t xTaskEventGroup;    // Task communication bits (DATA_READY, DATA_ERROR, etc.)
    EventGroupHandle_t xSensorEventGroup;  // Interleaved update/error bits: U0 E0 U1 E1 ... U7 E7
    EventGroupHandle_t xInitEventGroup;    // Initialization step bits

    // Initialization event group uses bits 0-23 (no shifting needed)
    // InitBits are used directly in xInitEventGroup

    SemaphoreHandle_t initMutex;
    SemaphoreHandle_t interfaceMutex;  // For IDeviceInstance interface

    // Task handles for notifications
    TaskHandle_t dataReceiverTask;     // Task to notify when data arrives

    // Callback storage
    TickType_t lastReportReceivedTime;
    TimerHandle_t missedReportTimer;
    std::function<void(uint8_t functionCode, const uint8_t* data, uint16_t length)> modbusResponseCallback;

    // Expected update interval in milliseconds
    static uint32_t expectedUpdateIntervalMs;
    
    // Timeout multipliers for different contexts
    static constexpr uint32_t RESPONSIVENESS_CHECK_MULTIPLIER = 3;  // 3x for isModuleResponsive()
    static constexpr uint32_t MONITORING_CHECK_MULTIPLIER = 5;     // 5x for monitoring task

    // Constants for timing and initialization
    static constexpr TickType_t INIT_STEP_TIMEOUT = pdMS_TO_TICKS(300);     // 300ms per step
    static constexpr TickType_t INIT_TOTAL_TIMEOUT = pdMS_TO_TICKS(1500);   // 1.5s total
    static constexpr TickType_t INTER_REQUEST_DELAY = pdMS_TO_TICKS(10);    // 10ms between requests

    // Register addresses
    static constexpr uint16_t CONNECTION_STATUS_START_REGISTER = 0;            // Starting register for connection status
    static constexpr uint16_t MEASUREMENT_RANGE_REGISTER = 76;                 // Measurement range register
    static constexpr uint16_t CHANNEL_CONFIG_REGISTER_START = 128;             // Start of channel configuration registers
    static constexpr uint16_t TEMPERATURE_REGISTER_START = 0;                  // Start of temperature registers

    // Optional settings registers
    static constexpr uint16_t MODULE_TEMPERATURE_REGISTER = 67;                // Module temperature register
    static constexpr uint16_t RS485_ADDRESS_REGISTER = 70;                     // RS485 Address register
    static constexpr uint16_t BAUD_RATE_REGISTER = 71;                         // RS485 Baud Rate register
    static constexpr uint16_t PARITY_REGISTER = 72;                            // RS485 Parity register

    // Response timeout settings
    static constexpr TickType_t MB8ART_RESPONSE_TIMEOUT_MS = pdMS_TO_TICKS(1000);
    static constexpr uint8_t RETRY_COUNT = MB8ART_RETRY_COUNT;  // Use macro value
    static constexpr TickType_t MB8ART_INTER_COMMAND_DELAY_MS = pdMS_TO_TICKS(50);

    // Sensor operation safety limits (similar to relay safety in RYN4)
    static constexpr TickType_t MIN_SENSOR_READ_INTERVAL_MS = pdMS_TO_TICKS(100);
    static constexpr uint32_t MAX_SENSOR_READ_RATE_PER_MIN = 60;

    // General Packet Lengths
    static constexpr size_t EXPECTED_RS485_PACKET_LENGTH = 2;                  // 1 register = 2 bytes
    static constexpr size_t EXPECTED_BAUD_RATE_PACKET_LENGTH = 2;              // Baud rate packet length
    static constexpr size_t EXPECTED_PARITY_PACKET_LENGTH = 2;                 // Parity packet length
    static constexpr size_t EXPECTED_MODULE_TEMP_PACKET_LENGTH = 2;            // Module temperature packet length
    static constexpr size_t EXPECTED_MEASUREMENT_RANGE_PACKET_LENGTH = 2;      // Measurement range packet length
    static constexpr size_t EXPECTED_CHANNEL_CONFIG_PACKET_LENGTH = 2;         // Single channel config packet length
    static constexpr size_t EXPECTED_ALL_CHANNEL_CONFIG_PACKET_LENGTH = DEFAULT_NUMBER_OF_SENSORS * 2;
    static constexpr size_t EXPECTED_TEMPERATURE_PACKET_LENGTH = DEFAULT_NUMBER_OF_SENSORS * 2;

    // Value range constants
    static constexpr uint16_t MAX_BAUD_RATE_VALUE = 7;                         // Maximum valid value for baud rate
    static constexpr uint16_t MAX_PARITY_VALUE = 2;                            // Maximum valid value for parity
    static constexpr int16_t TEMPERATURE_INVALID_THRESHOLD = 2990;  // 299.0°C in tenths
    static constexpr size_t NUM_CONNECTION_REGISTERS = 8;                      // Number of connection status registers
    static constexpr size_t CONNECTION_STATUS_PACKET_LENGTH = 8;               // Expected length of connection status packet
};

#endif // MB8ART_H
