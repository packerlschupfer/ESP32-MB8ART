// src/MB8ART.cpp

#include "MB8ART.h"
#include <unordered_map>
#include <string>
#include <cmath>  // For NAN
#include "ModbusDevice.h"  // For ModbusDevice base class
#include <ModbusErrorTracker.h>

using namespace mb8art;

// Static member definitions are now in MB8ARTSharedResources.cpp

MB8ART::MB8ART(uint8_t sensorAddress, const char* tag)
    : QueuedModbusDevice(sensorAddress),  // Pass server address to base class
      tag(tag),
      statusFlags{0, 0, 0, 0},  // Initialize all flags to 0
      sensorConnected(0),        // All sensors initially disconnected
      hardwareConfig(nullptr),   // Hardware config set via setHardwareConfig()
      sensorBindings({}),        // Initialize all bindings to nullptr
      dataReceiverTask(nullptr) {
    
    // Pre-initialize MB8ARTSharedResources to ensure Logger is ready
    // This forces singleton creation early, preventing any blocking during first log
    MB8ARTSharedResources::getInstance();
    
    // Logger is now handled by global Logger::getInstance()
    // No need for special initialization

    MB8ART_LOG_INIT_STEP("Creating MB8ART instance");

    // Create event groups (RYN4 interleaved pattern - saves one event group)
    xTaskEventGroup = xEventGroupCreate();   // Task communication bits
    xSensorEventGroup = xEventGroupCreate(); // Interleaved update/error bits: U0 E0 U1 E1 ... U7 E7
    xInitEventGroup = xEventGroupCreate();   // Initialization step bits

    // Create initialization mutex
    initMutex = xSemaphoreCreateMutex();

    // Create interface mutex for IDeviceInstance
    interfaceMutex = xSemaphoreCreateMutex();

    // Immediately check if creation was successful
    if (!xTaskEventGroup || !xSensorEventGroup || !xInitEventGroup ||
        !initMutex || !interfaceMutex) {
        LOG_MB8ART_ERROR_NL("Failed to create event groups or mutexes");
        cleanup();
        return;
    }

    // Clear all event bits in all groups
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xTaskEventGroup, 0x00FFFFFF);
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, 0x00FFFFFF);
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xInitEventGroup, 0x00FFFFFF);

    // Initialize data structures
    initializeDataStructures();
    
    // Register callback with ModbusDevice system
    registerModbusResponseCallback(
        std::bind(&MB8ART::processModbusResponse, this, 
                 std::placeholders::_1, 
                 std::placeholders::_2, 
                 std::placeholders::_3));
                 
    MB8ART_LOG_INIT_STEP("MB8ART instance created successfully");
}

MB8ART::~MB8ART() {
    cleanup();
}

void MB8ART::cleanup() {
    // Clean up task event group
    if (xTaskEventGroup) {
        vEventGroupDelete(xTaskEventGroup);
        xTaskEventGroup = nullptr;
    }

    // Clean up sensor event group (interleaved update/error bits)
    if (xSensorEventGroup) {
        vEventGroupDelete(xSensorEventGroup);
        xSensorEventGroup = nullptr;
    }

    // Clean up initialization event group
    if (xInitEventGroup) {
        vEventGroupDelete(xInitEventGroup);
        xInitEventGroup = nullptr;
    }

    // Clean up mutexes
    if (initMutex) {
        vSemaphoreDelete(initMutex);
        initMutex = nullptr;
    }

    if (interfaceMutex) {
        vSemaphoreDelete(interfaceMutex);
        interfaceMutex = nullptr;
    }
    

    // Ensure device is unregistered from global map
    unregisterDevice();
    
    // Log cleanup
    LOG_MB8ART_INFO_NL("MB8ART device cleanup complete");
}

// initializeDataStructures is now in MB8ARTState.cpp

bool MB8ART::initializeDevice() {
    if (getInitPhase() == InitPhase::READY) {
        LOG_MB8ART_WARN_NL("Device already initialized");
        return true;
    }

    LOG_MB8ART_INFO_NL("Starting MB8ART initialization for address 0x%02X", getServerAddress());

    // Register device with ModbusDevice system
    if (registerDevice() != ModbusError::SUCCESS) {
        LOG_MB8ART_ERROR_NL("Failed to register device with ModbusDevice system");
        return false;
    }
    
    // Call configure() to do the actual initialization
    if (!configure()) {
        LOG_MB8ART_ERROR_NL("Configuration failed");
        return false;
    }
    
    return statusFlags.initialized;
}

// Registration is now handled by base class ModbusDevice

// batchReadAllConfig is now in MB8ARTConfig.cpp

// Initialization with error handling and batch config reading
bool MB8ART::configure() {
    MB8ART_STACK_CHECK_START();
    MB8ART_PERF_START(init_module);
    
    // Record start time for accurate duration measurement
    TickType_t initStartTime = xTaskGetTickCount();
    
    MutexGuard initGuard(initMutex, pdMS_TO_TICKS(500));
    if (!initGuard.hasLock()) {
        LOG_MB8ART_ERROR_NL("Failed to acquire init mutex");
        return false;
    }

    MB8ART_LOG_INIT_STEP("Starting synchronous initialization");
    
    // CRITICAL: Ensure async mode is disabled during initialization
    // This prevents sync/async register operation mismatch
    if (isAsyncEnabled()) {
        LOG_MB8ART_WARN_NL("Async mode was enabled - disabling for initialization");
        disableAsync();
    }
    
    // Set init phase to CONFIGURING to ensure sync operations work correctly
    setInitPhase(InitPhase::CONFIGURING);

    // Reset channel configuration tracking
    channelsConfiguredDuringInit = 0;

    // Create init event group if needed
    if (!xInitEventGroup) {
        xInitEventGroup = xEventGroupCreate();
        if (!xInitEventGroup) {
            LOG_MB8ART_ERROR_NL("Failed to create init event group");
            return false;
        }
    }
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xInitEventGroup, InitBits::ALL_BITS);

    // Declare activeCount at method scope
    int activeCount = 0;

    // Try batch read with retries - this also serves as device probe
    // If batch read succeeds, device is responsive (no separate probe needed)
    const int MAX_BATCH_ATTEMPTS = 3;
    bool batchSuccess = false;

    for (int attempt = 0; attempt < MAX_BATCH_ATTEMPTS && !batchSuccess; attempt++) {
        if (attempt > 0) {
            LOG_MB8ART_DEBUG_NL("Batch read attempt %d/%d", attempt + 1, MAX_BATCH_ATTEMPTS);
            vTaskDelay(pdMS_TO_TICKS(50 * attempt)); // Progressive backoff: 50ms, 100ms
        }

        batchSuccess = batchReadAllConfig();
    }

    if (batchSuccess) {
        statusFlags.moduleOffline = 0;  // Device responded - mark as online
    }
    
    if (!batchSuccess) {
        // Fall back to individual reads if batch read failed
        LOG_MB8ART_WARN_NL("Batch read failed after %d attempts, falling back to individual reads", MAX_BATCH_ATTEMPTS);

        // STEP 1: Read measurement range synchronously
        MB8ART_LOG_INIT_STEP("Reading measurement range...");

        auto rangeResult = readHoldingRegisters(MEASUREMENT_RANGE_REGISTER, 1);
        if (!rangeResult.isOk()) {
            auto category = modbus::ModbusErrorTracker::categorizeError(rangeResult.error());
            modbus::ModbusErrorTracker::recordError(getServerAddress(), category);
            LOG_MB8ART_ERROR_NL("Failed to read measurement range - device offline (error: %d)",
                               static_cast<int>(rangeResult.error()));
            statusFlags.moduleOffline = 1;  // Mark device as offline
            MB8ART_PERF_END(init_module, "Module initialization (failed)");
            return false;
        }
        modbus::ModbusErrorTracker::recordSuccess(getServerAddress());

        // Device responded - mark as online
        statusFlags.moduleOffline = 0;
    
    currentRange = static_cast<mb8art::MeasurementRange>(rangeResult.value()[0] & 0x01);
    LOG_MB8ART_DEBUG_NL("Measurement range: %s", 
                      (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES (0.01°C)" : "LOW_RES (0.1°C)");
    setInitializationBit(InitBits::MEASUREMENT_RANGE);
    setInitializationBit(InitBits::DEVICE_RESPONSIVE);
    
    // STEP 2: Read module temperature and settings
    MB8ART_LOG_INIT_STEP("Reading module settings...");
    
    // Read module temperature
    auto tempResult = readHoldingRegisters(MODULE_TEMPERATURE_REGISTER, 1);
    if (tempResult.isOk() && !tempResult.value().empty()) {
        moduleSettings.moduleTemperature = tempResult.value()[0] * 0.1f;
        moduleSettings.isTemperatureValid = true;
        LOG_MB8ART_DEBUG_NL("Module temperature: %.1f°C", moduleSettings.moduleTemperature);
    }
    
    // Read RS485 address
    auto addrResult = readHoldingRegisters(RS485_ADDRESS_REGISTER, 1);
    if (addrResult.isOk() && !addrResult.value().empty()) {
        moduleSettings.rs485Address = addrResult.value()[0] & 0xFF;
        LOG_MB8ART_DEBUG_NL("RS485 address: 0x%02X", moduleSettings.rs485Address);
    }
    
    // Read baud rate
    auto baudResult = readHoldingRegisters(BAUD_RATE_REGISTER, 1);
    if (baudResult.isOk() && !baudResult.value().empty()) {
        moduleSettings.baudRate = baudResult.value()[0] & 0xFF;
        LOG_MB8ART_DEBUG_NL("Baud rate code: %d", moduleSettings.baudRate);
    }
    
    // Read parity
    auto parityResult = readHoldingRegisters(PARITY_REGISTER, 1);
    if (parityResult.isOk() && !parityResult.value().empty()) {
        moduleSettings.parity = parityResult.value()[0] & 0xFF;
        LOG_MB8ART_DEBUG_NL("Parity code: %d", moduleSettings.parity);
    }
    
    // STEP 3: Read channel configurations
    MB8ART_LOG_INIT_STEP("Reading channel configurations...");
    
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        auto configResult = readHoldingRegisters(CHANNEL_CONFIG_REGISTER_START + i, 1);
        if (!configResult.isOk()) {
            auto category = modbus::ModbusErrorTracker::categorizeError(configResult.error());
            modbus::ModbusErrorTracker::recordError(getServerAddress(), category);
            LOG_MB8ART_ERROR_NL("Failed to read config for channel %d", i);
            return false;
        }
        modbus::ModbusErrorTracker::recordSuccess(getServerAddress());

        processChannelConfig(i, configResult.value()[0]);
    }
    
    // Update the pre-computed active channel mask after reading all channels
    updateActiveChannelMask();
    
    setInitializationBit(InitBits::CHANNEL_CONFIG);
    
    
    } // End of if block (fallback path)
    
    // Count active channels and build summary (common for both paths)
    activeCount = 0;
    std::string activeChannelList;
    
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (channelConfigs[i].mode != static_cast<uint16_t>(mb8art::ChannelMode::DEACTIVATED)) {
            activeCount++;
            if (!activeChannelList.empty()) {
                activeChannelList += ", ";
            }
            activeChannelList += "Ch" + std::to_string(i);
        }
    }
    
    if (batchSuccess) {
        LOG_MB8ART_DEBUG_NL("Ultra-fast initialization completed in 2 batch reads!");
    }
    
    LOG_MB8ART_INFO_NL("Channels configured - Active: %d/%d [%s]", 
                      activeCount, DEFAULT_NUMBER_OF_SENSORS, 
                      activeCount > 0 ? activeChannelList.c_str() : "None");
    
    // STEP 4: Check if we have minimum required initialization (common for both paths)
    if (checkAllInitBitsSet()) {
        statusFlags.initialized = 1;
        
        // Set completion flags
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xTaskEventGroup, INIT_COMPLETE_BIT);
        MB8ART_LOG_INIT_COMPLETE();
        
        // Clear all sensor event bits to start fresh
        clearDataEventBits();
        
        // Calculate initialization duration
        TickType_t initDuration = xTaskGetTickCount() - initStartTime;
        
        // Print summary
        LOG_MB8ART_INFO_NL("=== Fast Initialization Complete ===");
        LOG_MB8ART_INFO_NL("Duration: %d ms", pdTICKS_TO_MS(initDuration));
        LOG_MB8ART_INFO_NL("Active Channels: %d", activeCount);
        LOG_MB8ART_INFO_NL("Measurement Range: %s", 
                          (currentRange == mb8art::MeasurementRange::HIGH_RES) ? "HIGH_RES" : "LOW_RES");
        LOG_MB8ART_INFO_NL("Device Address: 0x%02X", getServerAddress());
        
        // Show optional settings if received
        if (moduleSettings.rs485Address != 0) {
            LOG_MB8ART_INFO_NL("Baud Rate: %s", 
                              baudRateToString(getBaudRateEnum(moduleSettings.baudRate)).c_str());
            if (moduleSettings.isTemperatureValid) {
                LOG_MB8ART_INFO_NL("Module Temperature: %.1f°C", moduleSettings.moduleTemperature);
            }
        }
        
        // If we have active channels, print minimal config
        if (activeCount > 0) {
            LOG_MB8ART_INFO_NL("Ready for temperature readings");
        } else {
            LOG_MB8ART_WARN_NL("No active channels - configure channels before reading");
        }
        
        LOG_MB8ART_INFO_NL("===================================");
        
        // Mark optional settings as no longer pending since batch read got them
        statusFlags.optionalSettingsPending = 0;
        
    } else {
        // Initialization failed
        EventBits_t initBits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
        
        TickType_t elapsedTime = xTaskGetTickCount() - initStartTime;
        LOG_MB8ART_ERROR_NL("Initialization incomplete after %dms. Device at address %d may be offline or misconfigured",
                           pdTICKS_TO_MS(elapsedTime), getServerAddress());
        
        // Log which specific steps failed
        if (!(initBits & InitBits::DEVICE_RESPONSIVE)) {
            LOG_MB8ART_ERROR_NL("  - Device not responsive (check wiring/address/power)");
        }
        if (!(initBits & InitBits::MEASUREMENT_RANGE)) {
            LOG_MB8ART_ERROR_NL("  - Failed to read measurement range from register 0x%04X", MEASUREMENT_RANGE_REGISTER);
        }
        if (!(initBits & InitBits::CHANNEL_CONFIG)) {
            LOG_MB8ART_ERROR_NL("  - Failed to read channel config from registers 0x%04X-0x%04X",
                              CHANNEL_CONFIG_REGISTER_START, CHANNEL_CONFIG_REGISTER_START + 7);
        }
        
        LOG_MB8ART_ERROR_NL("Initialization status bits: 0x%02X (expected: 0x%02X)", initBits, InitBits::ALL_BITS);
        
        statusFlags.initialized = 0;
        statusFlags.moduleOffline = 1;  // Mark as offline if initialization failed
    }
    
    // Note: initMutex is automatically released by MutexGuard destructor
    // Do NOT manually release it here as that causes a double-release
    
    MB8ART_STACK_CHECK_END("initializeModuleSettings");
    MB8ART_PERF_END(init_module, "Fast module initialization");
    
    // Enable queued mode after successful initialization
    if (statusFlags.initialized) {
        // IMPORTANT: Set init phase to READY before enabling async
        // This ensures the QueuedModbusDevice will route responses to async queue
        setInitPhase(InitPhase::READY);
        LOG_MB8ART_DEBUG_NL("Set init phase to READY");
        
        // Small delay to ensure any pending responses are processed
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Now enable async mode
        if (enableAsync(MB8ART_ASYNC_QUEUE_SIZE)) {
            LOG_MB8ART_INFO_NL("Async mode enabled successfully with %d slots", MB8ART_ASYNC_QUEUE_SIZE);
        } else {
            LOG_MB8ART_ERROR_NL("Failed to enable async mode - continuing in sync mode");
            // Device can still work in sync mode
        }
    } else {
        // Set ERROR phase if initialization failed
        setInitPhase(InitPhase::ERROR);
    }
    
    return statusFlags.initialized;  // Return initialization status
}

// readOptionalSettings is now in MB8ARTConfig.cpp

// State tracking methods are now in MB8ARTState.cpp

// Module responsiveness check

/* waitForInitializationComplete removed - not needed with new architecture
*/

// waitForInitStep method
// waitForInitStep is now in MB8ARTState.cpp

// setDataReceiverTask is now in MB8ARTConfig.cpp

// Notification helper
// notifyDataReceiver is now in MB8ARTEvents.cpp

// Remove waitForInitialization - no longer needed with new architecture

// Handle Modbus responses


// processTemperatureData is now in MB8ARTSensor.cpp

// processChannelData is now in MB8ARTSensor.cpp

// updateSensorReading is now in MB8ARTSensor.cpp

// updateEventBits is now in MB8ARTEvents.cpp

// waitForData to use single event group

/* REMOVED - processData no longer needed with new architecture
*/

// requestData removed - use requestTemperatures() instead
/*
*/

// reqAddress is now in MB8ARTConfig.cpp

// reqBaudRate is now in MB8ARTConfig.cpp

// reqParity is now in MB8ARTConfig.cpp

// reqModuleTemperature is now in MB8ARTConfig.cpp

// reqMeasurementRange is now in MB8ARTConfig.cpp

// handleSensorError is now in MB8ARTModbus.cpp

// Implementation of markChannelDeactivated with char buffer
// markChannelDeactivated is now in MB8ARTSensor.cpp

// processModbusResponse is now in MB8ARTModbus.cpp

// Configuration methods with error codes

// Enhanced error handling for configuration

// Enhanced batch configuration with error codes



void MB8ART::printChannelDiagnostics() {
    // Update connection status before printing diagnostics
    LOG_MB8ART_DEBUG_NL("Updating connection status before diagnostics");
    if (!refreshConnectionStatus()) {
        LOG_MB8ART_WARN_NL("Failed to refresh connection status");
    } else {
        // Wait for the FC=2 response to be processed.
        vTaskDelay(pdMS_TO_TICKS(400)); // Use defined timeout
    }
    LOG_MB8ART_INFO_NL("=== Channel Diagnostics ===");
    
    int activeCount = 0;
    int connectedCount = 0;
    int errorCount = 0;
    int validDataCount = 0;
    
    for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        mb8art::ChannelMode mode = static_cast<mb8art::ChannelMode>(channelConfigs[i].mode);
        
        if (mode != mb8art::ChannelMode::DEACTIVATED) {
            activeCount++;
            
            std::string status = "ACTIVE";
            
            // Use the actual connection status from Modbus discrete inputs
            if (isSensorConnected(i)) {
                connectedCount++;
                status += "/CONNECTED";
            } else {
                status += "/DISCONNECTED";
            }
            
            if (sensorReadings[i].Error) {
                errorCount++;
                status += "/ERROR";
            }
            
            if (sensorReadings[i].isTemperatureValid) {
                validDataCount++;
                LOG_MB8ART_INFO_NL("Channel %d: %s - %.2f°C", i, status.c_str(), 
                                  sensorReadings[i].temperature);
            } else {
                LOG_MB8ART_INFO_NL("Channel %d: %s - No Valid Data", i, status.c_str());
            }
        } else {
            LOG_MB8ART_INFO_NL("Channel %d: DEACTIVATED", i);
        }
    }
    
    LOG_MB8ART_INFO_NL("Summary: %d active, %d connected, %d valid readings, %d errors", 
                      activeCount, connectedCount, validDataCount, errorCount);
}



// getSubTypeString is now in MB8ARTState.cpp

// validateChannelConfig is now in MB8ARTModbus.cpp

// processThermocoupleData is now in MB8ARTSensor.cpp

// processPTData is now in MB8ARTSensor.cpp

// processVoltageData is now in MB8ARTSensor.cpp

// processChannelConfig is now in MB8ARTSensor.cpp

// processCurrentData is now in MB8ARTSensor.cpp

// convertRawToTemperature is now in MB8ARTSensor.cpp

// applyTemperatureCorrection is now in MB8ARTSensor.cpp

// isTemperatureInRange is now in MB8ARTState.cpp

// registerModbusResponseCallback is now in MB8ARTModbus.cpp

// registerTemperatureCallback is now in MB8ARTSensor.cpp

// reqAllChannelModes is now in MB8ARTConfig.cpp

// reqChannelMode is now in MB8ARTConfig.cpp


// Modbus callback methods are now in MB8ARTModbus.cpp

// setUpdateEventBits is now in MB8ARTEvents.cpp

// clearUpdateEventBits is now in MB8ARTEvents.cpp

// setErrorEventBits is now in MB8ARTEvents.cpp

// clearErrorEventBits is now in MB8ARTEvents.cpp


// printSensorReading is now in MB8ARTSensor.cpp

// getStoredBaudRate and getStoredParity are now in MB8ARTState.cpp

// baudRateToString is now in MB8ARTConfig.cpp

// parityToString is now in MB8ARTConfig.cpp

// getBaudRateEnum is now in MB8ARTConfig.cpp

// getParityEnum is now in MB8ARTConfig.cpp

// handleModbusError is now in MB8ARTModbus.cpp

// setInitializationBit with all safety checks and logging
void MB8ART::setInitializationBit(EventBits_t bit) {
    if (!xInitEventGroup) {
        LOG_MB8ART_ERROR_NL("xInitEventGroup is NULL - cannot set bit 0x%02X", bit);
        return;
    }
    
    // Get current bits
    EventBits_t currentBits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
    
    // Check if bit is already set
    if ((currentBits & bit) == bit) {
        MB8ART_DEBUG_ONLY(
            LOG_MB8ART_DEBUG_NL("Init bit 0x%02X already set (current: 0x%02X)", bit, currentBits);
        );
        return;
    }
    
    // Set the bit
    MB8ART_SRP_EVENT_GROUP_SET_BITS(xInitEventGroup, bit);
    
    // Log the change
    #if defined(MB8ART_DEBUG_FULL)
        EventBits_t afterBits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
        LOG_MB8ART_DEBUG_NL("Set init bit 0x%02X: 0x%02X -> 0x%02X", bit, currentBits, afterBits);
    #elif defined(MB8ART_DEBUG_SELECTIVE)
        EventBits_t afterBits __attribute__((unused)) = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
        LOG_MB8ART_DEBUG_NL("Init bits changed: 0x%02X -> 0x%02X", currentBits, afterBits);
    #endif
}

bool MB8ART::checkAllInitBitsSet() const {
    if (!xInitEventGroup) {
        LOG_MB8ART_ERROR_NL("Init event group is null");
        return false;
    }
    
    EventBits_t bits = MB8ART_SRP_EVENT_GROUP_GET_BITS(xInitEventGroup);
    bool allSet = (bits & InitBits::ALL_BITS) == InitBits::ALL_BITS;
    
    if (!allSet) {
        EventBits_t missingBits = InitBits::ALL_BITS & ~bits;
        #ifdef MB8ART_DEBUG
            // Only log errors if we're past initialization phase
            // During init, these are expected and shouldn't spam the logs
            if (statusFlags.initialized) {
                LOG_MB8ART_ERROR_NL("Missing initialization bits: 0x%02X", missingBits);
                
                // Log specific missing components
                if (!(bits & InitBits::MEASUREMENT_RANGE))
                    LOG_MB8ART_ERROR_NL("Missing: Measurement Range");
                if (!(bits & InitBits::CHANNEL_CONFIG))
                    LOG_MB8ART_ERROR_NL("Missing: Channel Configuration");
                if (!(bits & InitBits::DEVICE_RESPONSIVE))
                    LOG_MB8ART_ERROR_NL("Missing: Device Responsive");
            }
        #else
            (void)missingBits;  // Suppress warning in release mode
        #endif
    } else {
        LOG_MB8ART_DEBUG_NL("All initialization bits are set");
    }
    
    return allSet;
}

/* performAction removed - not needed for sensor device
*/
    
bool MB8ART::requestConnectionStatus() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("requestConnectionStatus blocked - device is offline");
        return false;
    }
    
    // Read discrete inputs for connection status
    auto result = readDiscreteInputs(CONNECTION_STATUS_START_REGISTER, DEFAULT_NUMBER_OF_SENSORS);

    if (!result.isOk()) {
        auto category = modbus::ModbusErrorTracker::categorizeError(result.error());
        modbus::ModbusErrorTracker::recordError(getServerAddress(), category);
        LOG_MB8ART_ERROR_NL("Failed to request connection status");
        handleDisconnection();
        return false;
    }
    modbus::ModbusErrorTracker::recordSuccess(getServerAddress());
    return true;
}

void MB8ART::updateConnectionStatus(uint8_t channel, bool connected) {
    if (channel >= DEFAULT_NUMBER_OF_SENSORS) {
        LOG_MB8ART_ERROR_NL("Invalid channel index: %d", channel);
        return;
    }

    bool currentlyConnected = isSensorConnected(channel);
    if (currentlyConnected != connected) {
        setSensorConnected(channel, connected);
        LOG_MB8ART_INFO_NL("Channel %d connection status changed to: %s", 
                         channel, connected ? "Connected" : "Disconnected");
    }
}

bool MB8ART::refreshConnectionStatus() {
    // Prevent polling if device is offline
    if (statusFlags.moduleOffline) {
        LOG_MB8ART_DEBUG_NL("refreshConnectionStatus blocked - device is offline");
        return false;
    }
    
    // Check cache first
    TickType_t now = xTaskGetTickCount();
    TickType_t timeSinceLastCheck = now - lastConnectionStatusCheck;
    
    if (lastConnectionStatusCheck != 0 && timeSinceLastCheck < pdMS_TO_TICKS(CONNECTION_STATUS_CACHE_MS)) {
        // Use cached data
        MB8ART_DEBUG_ONLY(
            LOG_MB8ART_DEBUG_NL("Using cached connection status (age: %d ms)", 
                               pdTICKS_TO_MS(timeSinceLastCheck));
        );
        return true; // Return cached success
    }
    
    LOG_MB8ART_INFO_NL("=== refreshConnectionStatus - cache expired, requesting fresh data ===");
    bool result = requestConnectionStatus();
    
    if (result) {
        // Update cache timestamp on successful request
        lastConnectionStatusCheck = now;
    }
    
    return result;
}

// handleConnectionStatus and handleDisconnection are now in MB8ARTModbus.cpp

// Enhanced getData with improved caching and validation

// Additional getter methods for enhanced functionality

// setTag is now in MB8ARTConfig.cpp

// updateSensorEventBits is now in MB8ARTEvents.cpp

// clearDataEventBits is now in MB8ARTEvents.cpp

// hasRecentSensorData is now in MB8ARTSensor.cpp

// batchReadInitialConfig is now in MB8ARTConfig.cpp

// clearPendingResponses is now in MB8ARTModbus.cpp

// probeDevice is now in MB8ARTDevice.cpp

// SimpleModbusDevice provides the IModbusAnalogInput interface
// We don't need to implement these methods as SimpleModbusDevice handles them

// MB8ART specific methods
// requestTemperatures is now in MB8ARTSensor.cpp

// readChannelData is now in MB8ARTDevice.cpp

// updateActiveChannelMask is now in MB8ARTState.cpp

// IDeviceInstance interface implementation






