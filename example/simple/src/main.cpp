// main.cpp
// 
// MB8ART Example Application using ModbusDevice Architecture
// 
// This example demonstrates the new ModbusDevice architecture where:
// 1. Global ModbusRTU instance is set via setGlobalModbusRTU() before device creation
// 2. Devices are registered in a global map for automatic response routing
// 3. MB8ART inherits from QueuedModbusDevice for hybrid sync/async operation
// 4. Synchronous reads are used during initialization, then async for normal operation
// 5. No need to pass modbusMaster to device constructors anymore
//
// Key initialization sequence:
// 1. Initialize Serial1 for RS485
// 2. Call setGlobalModbusRTU(&modbusMaster)
// 3. Setup modbusMaster callbacks (mainHandleData, handleError)
// 4. Create MB8ART instance
// 5. Call device->initialize() (registration happens automatically)
// 6. Wait for initialization completion
// 7. Start FreeRTOS tasks for temperature monitoring
//
#include <Arduino.h>
#include "config/ProjectConfig.h"

// Core system includes
#include <TaskManager.h>
#ifdef USE_CUSTOM_LOGGER
    #include <Logger.h>
    #include <LogInterfaceImpl.cpp>  // Include implementation, not header
    #include <ConsoleBackend.h>
#endif

// Network includes
#ifdef USE_WIFI
// WiFi is disabled in platformio.ini, so skip WiFi.h
// #include <WiFi.h>
#else
#include <EthernetManager.h>
#endif

// OTA includes
#include <ArduinoOTA.h>

// Task includes
#include "tasks/MonitoringTask.h"
#include "tasks/OTATask.h"
#include "tasks/TemperatureTask.h"
#include "tasks/DataProcessingTask.h"
#include "tasks/AlarmTask.h"

// MB8ART and Modbus includes
#include "MB8ART.h"
#include <ModbusDevice.h>  // For ModbusRegistry
#include <esp32ModbusRTU.h>
#include <memory>

// Demo includes (optional - enable by defining RUN_OPTIMIZATION_DEMO)
#ifdef RUN_OPTIMIZATION_DEMO
#include "MB8ARTOptimizationDemo.h"
#endif

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "utils/WatchdogHelper.h"

// Global objects
#ifdef USE_CUSTOM_LOGGER
    Logger logger(std::make_shared<ConsoleBackend>());
#endif
TaskManager taskManager;

// MB8ART globals
MB8ART* temperatureModule = nullptr;
esp32ModbusRTU modbusMaster(&Serial1);

// mainHandleData and handleError are provided by ModbusDevice.h
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

// Network status
bool networkConnected = false;

// Wrapper for esp32ModbusRTU error handler
void modbusErrorHandler(esp32Modbus::Error error) {
    // Since we don't know which device caused the error, log it
    LOG_ERROR(LOG_TAG_MODBUS, "Modbus error: 0x%02X", static_cast<uint8_t>(error));
    
    // Call the global handler with a dummy address (0)
    // The actual device will be determined by pending requests
    handleError(0, error);
}

// Function declarations
void initializeHardware();
void initializeNetwork();
void initializeModbus();
void initializeMB8ART();
void initializeTasks();
void configureLogging();
void printSystemInfo();
void cleanup();

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { delay(10); }
    
    Serial.println("\n\n========================================");
    Serial.println("MB8ART Temperature Monitoring System");
    Serial.println("========================================\n");
    
    // Configure logging based on build mode
    configureLogging();
    
    // Initialize hardware
    initializeHardware();
    
    // Initialize network (WiFi or Ethernet)
    initializeNetwork();
    
    // Initialize Modbus and MB8ART
    initializeModbus();
    initializeMB8ART();
    
    // Initialize FreeRTOS tasks
    initializeTasks();
    
    // Print system information
    printSystemInfo();
    
    #ifdef RUN_OPTIMIZATION_DEMO
    // Run optimization demo only if MB8ART is initialized
    if (temperatureModule && temperatureModule->isInitialized() && !temperatureModule->isModuleOffline()) {
        LOG_INFO(LOG_TAG_MAIN, "Starting optimization demo in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        runMB8ARTOptimizationDemo(temperatureModule);
    } else {
        LOG_WARN(LOG_TAG_MAIN, "Skipping optimization demo - MB8ART not initialized or offline");
    }
    #endif
    
    LOG_INFO(LOG_TAG_MAIN, "System initialization complete");
}

void loop() {
    // Handle OTA updates if network is connected
    if (networkConnected) {
        ArduinoOTA.handle();
    }
    
    // Small delay to prevent watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));
}

void configureLogging() {
    #ifdef NO_LOGGER
        // Using ESP-IDF logging
        #if defined(LOG_MODE_DEBUG_FULL)
            esp_log_level_set("*", ESP_LOG_VERBOSE);
            Serial.println("Logging: DEBUG FULL MODE (ESP-IDF)");
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            esp_log_level_set("*", ESP_LOG_DEBUG);
            Serial.println("Logging: DEBUG SELECTIVE MODE (ESP-IDF)");
        #else
            esp_log_level_set("*", ESP_LOG_INFO);
            Serial.println("Logging: RELEASE MODE (ESP-IDF)");
        #endif
    #else
        // Using custom Logger
        #if defined(LOG_MODE_DEBUG_FULL)
            logger.setLogLevel(ESP_LOG_VERBOSE);
            Serial.println("Logging: DEBUG FULL MODE (Custom Logger)");
        #elif defined(LOG_MODE_DEBUG_SELECTIVE)
            logger.setLogLevel(ESP_LOG_DEBUG);
            Serial.println("Logging: DEBUG SELECTIVE MODE (Custom Logger)");
        #else
            logger.setLogLevel(ESP_LOG_INFO);
            Serial.println("Logging: RELEASE MODE (Custom Logger)");
        #endif
        
        // Configure MB8ART specific logging if using custom logger
        #ifdef MODBUSDEVICE_USE_CUSTOM_LOGGER
            logger.setTagLevel("MB8ART", ESP_LOG_INFO);
            logger.setTagLevel("ModbusD", ESP_LOG_INFO);
            logger.setTagLevel("ModbusRTU", ESP_LOG_WARN);
        #endif
    #endif
}

void initializeHardware() {
    LOG_INFO(LOG_TAG_MAIN, "Initializing hardware...");
    
    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize Serial1 for Modbus RS485
    Serial1.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    LOG_INFO(LOG_TAG_MAIN, "Serial1 initialized: %d baud, RX=%d, TX=%d", 
             MODBUS_BAUD_RATE, MODBUS_RX_PIN, MODBUS_TX_PIN);
    
    // Small delay for hardware stabilization
    delay(100);
}

void initializeNetwork() {
    LOG_INFO(LOG_TAG_MAIN, "Initializing network...");
    
    #ifdef USE_WIFI
        // WiFi initialization
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        LOG_INFO(LOG_TAG_NETWORK, "Connecting to WiFi: %s", WIFI_SSID);
        
        uint32_t startTime = millis();
        while (WiFi.status() != WL_CONNECTED && 
               (millis() - startTime) < WIFI_CONNECTION_TIMEOUT_MS) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            networkConnected = true;
            LOG_INFO(LOG_TAG_NETWORK, "WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
            
            // Configure hostname
            WiFi.setHostname(DEVICE_HOSTNAME);
            
            // Initialize OTA
            ArduinoOTA.setHostname(DEVICE_HOSTNAME);
            ArduinoOTA.setPassword(OTA_PASSWORD);
            ArduinoOTA.setPort(OTA_PORT);
            
            ArduinoOTA.onStart([]() {
                LOG_INFO(LOG_TAG_OTA, "OTA update started");
            });
            
            ArduinoOTA.onEnd([]() {
                LOG_INFO(LOG_TAG_OTA, "OTA update completed");
            });
            
            ArduinoOTA.onError([](ota_error_t error) {
                LOG_ERROR(LOG_TAG_OTA, "OTA error: %d", error);
            });
            
            ArduinoOTA.begin();
            LOG_INFO(LOG_TAG_OTA, "OTA service started on port %d", OTA_PORT);
        } else {
            LOG_WARN(LOG_TAG_NETWORK, "WiFi connection failed - continuing without network");
        }
    #else
        // Ethernet initialization would go here
        // Similar to RYN4 example but simplified for this demo
    #endif
}

void initializeModbus() {
    LOG_INFO(LOG_TAG_MAIN, "Initializing Modbus...");
    
    // Set the global ModbusRTU instance
    setGlobalModbusRTU(&modbusMaster);
    
    // Register global Modbus callbacks
    modbusMaster.onData(mainHandleData);
    modbusMaster.onError(modbusErrorHandler);
    
    // Start Modbus master
    modbusMaster.begin();
    
    LOG_INFO(LOG_TAG_MODBUS, "Modbus master started");
}

void initializeMB8ART() {
    LOG_INFO(LOG_TAG_MAIN, "Initializing MB8ART...");
    
    // Create MB8ART instance
    // Note: In production code, consider using std::unique_ptr for automatic cleanup
    // or ensure cleanup() is called before system reset/shutdown
    temperatureModule = new MB8ART(MB8ART_ADDRESS, "MB8ART");
    
    if (!temperatureModule) {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to create MB8ART instance!");
        return;
    }
    
    // Initialize the device (registration happens automatically)
    LOG_INFO(LOG_TAG_MAIN, "Starting MB8ART initialization...");
    uint32_t startTime = millis();
    
    bool initResult = temperatureModule->initialize();
    if (initResult) {
        LOG_INFO(LOG_TAG_MAIN, "MB8ART initialized successfully in %lu ms", 
                 millis() - startTime);
        
        // Print channel configuration
        temperatureModule->printChannelDiagnostics();
        
        // Configure measurement range if needed
        auto rangeResult = temperatureModule->configureMeasurementRange(
            MB8ART_HIGH_RESOLUTION ? mb8art::MeasurementRange::HIGH_RES 
                                  : mb8art::MeasurementRange::LOW_RES);
        
        if (rangeResult.isOk()) {
            LOG_INFO(LOG_TAG_MAIN, "Measurement range configured: %s", 
                     MB8ART_HIGH_RESOLUTION ? "HIGH_RES (0.01°C)" : "LOW_RES (0.1°C)");
        }
        
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "MB8ART initialization failed!");
        
        // Check if device is offline
        if (temperatureModule->isModuleOffline()) {
            LOG_ERROR(LOG_TAG_MAIN, "MB8ART device is OFFLINE - check wiring and power");
        }
    }
}

void initializeTasks() {
    LOG_INFO(LOG_TAG_MAIN, "Starting FreeRTOS tasks...");
    
    // Initialize monitoring task
    if (MonitoringTask::init() && MonitoringTask::start()) {
        LOG_INFO(LOG_TAG_MAIN, "Monitoring task started");
    } else {
        LOG_ERROR(LOG_TAG_MAIN, "Failed to start monitoring task");
    }
    
    // Check if MB8ART is initialized and not offline before starting temperature-related tasks
    if (temperatureModule && temperatureModule->isInitialized() && !temperatureModule->isModuleOffline()) {
        // Initialize temperature acquisition task
        if (TemperatureTask::init(temperatureModule) && TemperatureTask::start()) {
            LOG_INFO(LOG_TAG_MAIN, "Temperature task started");
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to start temperature task");
        }
        
        // Initialize data processing task
        if (DataProcessingTask::init(temperatureModule) && DataProcessingTask::start()) {
            LOG_INFO(LOG_TAG_MAIN, "Data processing task started");
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to start data processing task");
        }
        
        // Initialize alarm task
        if (AlarmTask::init(temperatureModule) && AlarmTask::start()) {
            LOG_INFO(LOG_TAG_MAIN, "Alarm task started");
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to start alarm task");
        }
    } else {
        LOG_WARN(LOG_TAG_MAIN, "Skipping temperature-related tasks - MB8ART not initialized or offline");
    }
    
    // Initialize OTA task if network is connected
    if (networkConnected) {
        if (OTATask::init() && OTATask::start()) {
            LOG_INFO(LOG_TAG_MAIN, "OTA task started");
        } else {
            LOG_ERROR(LOG_TAG_MAIN, "Failed to start OTA task");
        }
    }
}

void printSystemInfo() {
    Serial.println("\n========================================");
    Serial.println("System Information:");
    Serial.println("========================================");
    Serial.printf("Device Hostname: %s\n", DEVICE_HOSTNAME);
    Serial.printf("MB8ART Address: 0x%02X\n", MB8ART_ADDRESS);
    Serial.printf("Modbus Baud Rate: %d\n", MODBUS_BAUD_RATE);
    Serial.printf("Number of Channels: %d\n", MB8ART_NUM_CHANNELS);
    Serial.printf("High Resolution: %s\n", MB8ART_HIGH_RESOLUTION ? "Yes" : "No");
    
    #ifdef USE_WIFI
    if (networkConnected) {
        Serial.printf("WiFi SSID: %s\n", WIFI_SSID);
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    } else {
        Serial.println("Network: Not connected");
    }
    #endif
    
    Serial.printf("Free Heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("Chip Cores: %d\n", ESP.getChipCores());
    Serial.printf("CPU Frequency: %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());
    Serial.println("========================================\n");
}

void cleanup() {
    // Stop all tasks
    TemperatureTask::stop();
    DataProcessingTask::stop();
    AlarmTask::stop();
    MonitoringTask::stop();
    if (networkConnected) {
        OTATask::stop();
    }
    
    // Clean up MB8ART instance
    if (temperatureModule) {
        delete temperatureModule;
        temperatureModule = nullptr;
    }
    
    LOG_INFO(LOG_TAG_MAIN, "Cleanup completed");
}