// src/init/SystemInitializer.cpp
// MB8ART Full Example - System Initialization Implementation

#include "init/SystemInitializer.h"
#include <Arduino.h>
#include <esp_log.h>

#ifdef USE_CUSTOM_LOGGER
#include <Logger.h>
#include <LogInterfaceImpl.cpp>
#endif

#include <TaskManager.h>
#include <Watchdog.h>
#include <EthernetManager.h>
#include <ETH.h>
#include <OTAManager.h>
#include <esp32ModbusRTU.h>
#include <ModbusDevice.h>
#include <ModbusRegistry.h>
#include <MB8ART.h>

static const char* TAG = "SystemInit";

// Global instances
SystemInitializer* gSystemInitializer = nullptr;
TaskManager* gTaskManager = nullptr;
esp32ModbusRTU* gModbusMaster = nullptr;

// Forward declarations for tasks
void TemperatureTask(void* pvParameters);
void MonitoringTask(void* pvParameters);

// Modbus callbacks
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

static void modbusErrorHandler(esp32Modbus::Error error) {
    LOG_ERROR("Modbus", "Error: 0x%02X", static_cast<uint8_t>(error));
    handleError(0, error);
}

// =============================================================================
// SystemInitializer Implementation
// =============================================================================

SystemInitializer::SystemInitializer()
    : currentStage_(InitStage::NONE)
    , networkConnected_(false)
    , mb8art_(nullptr)
{
}

SystemInitializer::~SystemInitializer() {
    cleanup();
}

Result<void> SystemInitializer::initialize() {
    // Stage 1: Logging - MUST BE FIRST before any LOG_* calls!
    auto result = initializeLogging();
    if (result.isError()) {
        Serial.println("Logging initialization failed");
        return Result<void>::error();
    }
    currentStage_ = InitStage::LOGGING;

    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "========================================");
    LOG_INFO(TAG, "  %s v%s", PROJECT_NAME, FIRMWARE_VERSION);
    LOG_INFO(TAG, "========================================");
    LOG_INFO(TAG, "");

    // Stage 2: Hardware
    result = initializeHardware();
    if (result.isError()) {
        LOG_ERROR(TAG, "Hardware initialization failed");
        return Result<void>::error();
    }
    currentStage_ = InitStage::HARDWARE;

    // Stage 3: Network
    result = initializeNetwork();
    if (result.isError()) {
        LOG_WARN(TAG, "Network initialization failed (non-critical)");
        // Continue without network
    }
    currentStage_ = InitStage::NETWORK;

    // Stage 4: Modbus/MB8ART
    result = initializeModbus();
    if (result.isError()) {
        LOG_ERROR(TAG, "Modbus initialization failed");
        return Result<void>::error();
    }
    currentStage_ = InitStage::MODBUS;

    // Stage 5: Tasks
    result = initializeTasks();
    if (result.isError()) {
        LOG_ERROR(TAG, "Task initialization failed");
        return Result<void>::error();
    }
    currentStage_ = InitStage::TASKS;

    currentStage_ = InitStage::COMPLETE;

    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "========================================");
    LOG_INFO(TAG, "  System initialization complete!");
    LOG_INFO(TAG, "  Free heap: %d bytes", ESP.getFreeHeap());
    LOG_INFO(TAG, "========================================");
    LOG_INFO(TAG, "");

    return Result<void>::ok();
}

Result<void> SystemInitializer::initializeLogging() {
    LOG_INFO(TAG, "Initializing logging...");

#ifdef USE_CUSTOM_LOGGER
    Logger& logger = Logger::getInstance();
    logger.init(512);

    #ifdef LOG_MODE_RELEASE
        logger.setLogLevel(ESP_LOG_WARN);
    #elif defined(LOG_MODE_DEBUG_FULL)
        logger.setLogLevel(ESP_LOG_VERBOSE);
    #else
        logger.setLogLevel(ESP_LOG_INFO);
    #endif

    // Configure per-tag levels
    logger.setTagLevel("MB8ART", ESP_LOG_INFO);
    logger.setTagLevel("ModbusD", ESP_LOG_WARN);
    logger.setTagLevel("ModbusRTU", ESP_LOG_WARN);
    logger.setTagLevel("ETH", ESP_LOG_INFO);
    logger.setTagLevel("OTAMgr", ESP_LOG_INFO);
    logger.setTagLevel("TaskManager", ESP_LOG_WARN);

    logger.enableESPLogRedirection();
#else
    esp_log_level_set("*", ESP_LOG_INFO);
#endif

    // Suppress noisy ESP-IDF components
    esp_log_level_set("efuse", ESP_LOG_NONE);
    esp_log_level_set("cpu_start", ESP_LOG_NONE);
    esp_log_level_set("heap_init", ESP_LOG_NONE);
    esp_log_level_set("spi_flash", ESP_LOG_WARN);

    LOG_INFO(TAG, "Logging initialized");
    return Result<void>::ok();
}

Result<void> SystemInitializer::initializeHardware() {
    LOG_INFO(TAG, "Initializing hardware...");

    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // Initialize Serial1 for RS485
    Serial1.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    LOG_INFO(TAG, "RS485: %d baud, RX=%d, TX=%d", MODBUS_BAUD_RATE, MODBUS_RX_PIN, MODBUS_TX_PIN);

    delay(100);  // Hardware stabilization

    LOG_INFO(TAG, "Hardware initialized");
    return Result<void>::ok();
}

Result<void> SystemInitializer::initializeNetwork() {
    LOG_INFO(TAG, "Initializing network...");

    // Initialize Ethernet using static API
    auto initResult = EthernetManager::initialize();
    if (!initResult.isOk()) {
        LOG_WARN(TAG, "Ethernet initialization failed");
        return Result<void>::error();
    }

    // Wait for connection
    if (!EthernetManager::waitForConnection(ETH_CONNECTION_TIMEOUT_MS)) {
        LOG_WARN(TAG, "Ethernet connection timeout");
        return Result<void>::error();
    }

    networkConnected_ = true;
    LOG_INFO(TAG, "Ethernet connected: %s", ETH.localIP().toString().c_str());

    // Initialize OTA
    OTAManager::initialize(DEVICE_HOSTNAME, OTA_PASSWORD, OTA_PORT);
    LOG_INFO(TAG, "OTA ready on port %d", OTA_PORT);

    LOG_INFO(TAG, "Network initialized");
    return Result<void>::ok();
}

Result<void> SystemInitializer::initializeModbus() {
    LOG_INFO(TAG, "Initializing Modbus...");

    // Create Modbus master
    gModbusMaster = new esp32ModbusRTU(&Serial1);
    if (!gModbusMaster) {
        LOG_ERROR(TAG, "Failed to create Modbus master");
        return Result<void>::error();
    }

    // Set global ModbusRTU instance via ModbusRegistry
    modbus::ModbusRegistry::getInstance().setModbusRTU(gModbusMaster);

    // Register callbacks
    gModbusMaster->onData(mainHandleData);
    gModbusMaster->onError(modbusErrorHandler);

    // Start Modbus
    gModbusMaster->begin();
    LOG_INFO(TAG, "Modbus master started");

    // Create MB8ART device
    mb8art_ = new MB8ART(MB8ART_ADDRESS, "MB8ART");
    if (!mb8art_) {
        LOG_ERROR(TAG, "Failed to create MB8ART instance");
        return Result<void>::error();
    }

    // Initialize MB8ART
    LOG_INFO(TAG, "Initializing MB8ART at address 0x%02X...", MB8ART_ADDRESS);
    uint32_t startTime = millis();

    if (!mb8art_->initialize()) {
        LOG_ERROR(TAG, "MB8ART initialization failed");

        if (mb8art_->isModuleOffline()) {
            LOG_ERROR(TAG, "MB8ART is OFFLINE - check wiring and power");
        }
        return Result<void>::error();
    }

    LOG_INFO(TAG, "MB8ART initialized in %lu ms", millis() - startTime);

    // Configure measurement range
    auto rangeResult = mb8art_->configureMeasurementRange(
        MB8ART_HIGH_RESOLUTION ? mb8art::MeasurementRange::HIGH_RES
                               : mb8art::MeasurementRange::LOW_RES);

    if (rangeResult.isOk()) {
        LOG_INFO(TAG, "Measurement range: %s",
                 MB8ART_HIGH_RESOLUTION ? "HIGH_RES (0.01C)" : "LOW_RES (0.1C)");
    }

    // Print channel diagnostics
    mb8art_->printChannelDiagnostics();

    LOG_INFO(TAG, "Modbus initialized");
    return Result<void>::ok();
}

Result<void> SystemInitializer::initializeTasks() {
    LOG_INFO(TAG, "Initializing tasks...");

    // Create TaskManager with watchdog
    gTaskManager = new TaskManager(&Watchdog::getInstance());
    if (!gTaskManager) {
        LOG_ERROR(TAG, "Failed to create TaskManager");
        return Result<void>::error();
    }

    // Initialize watchdog
    // Suppress task_wdt logs during init - ESP-IDF internal logs conflict with Logger redirection
    esp_log_level_set("task_wdt", ESP_LOG_NONE);
    gTaskManager->initWatchdog(WATCHDOG_TIMEOUT_SECONDS, true);
    esp_log_level_set("task_wdt", ESP_LOG_WARN);

    // Temperature acquisition task
    auto tempConfig = TaskManager::WatchdogConfig::enabled(false, TEMPERATURE_INTERVAL_MS * 3);
    if (gTaskManager->startTask(
            TemperatureTask,
            "TempTask",
            STACK_SIZE_TEMPERATURE_TASK,
            mb8art_,
            PRIORITY_TEMPERATURE_TASK,
            tempConfig)) {
        LOG_INFO(TAG, "Temperature task started");
    } else {
        LOG_ERROR(TAG, "Failed to start temperature task");
        return Result<void>::error();
    }

    // Monitoring task
    auto monConfig = TaskManager::WatchdogConfig::enabled(false, MONITORING_INTERVAL_MS * 2);
    if (gTaskManager->startTask(
            MonitoringTask,
            "MonTask",
            STACK_SIZE_MONITORING_TASK,
            nullptr,
            PRIORITY_MONITORING_TASK,
            monConfig)) {
        LOG_INFO(TAG, "Monitoring task started");
    } else {
        LOG_WARN(TAG, "Failed to start monitoring task (non-critical)");
    }

    LOG_INFO(TAG, "Tasks initialized");
    return Result<void>::ok();
}

void SystemInitializer::cleanup() {
    LOG_INFO(TAG, "Cleaning up...");

    if (currentStage_ >= InitStage::TASKS) {
        cleanupTasks();
    }
    if (currentStage_ >= InitStage::MODBUS) {
        cleanupModbus();
    }
    if (currentStage_ >= InitStage::NETWORK) {
        cleanupNetwork();
    }
    if (currentStage_ >= InitStage::HARDWARE) {
        cleanupHardware();
    }

    currentStage_ = InitStage::NONE;
    LOG_INFO(TAG, "Cleanup complete");
}

void SystemInitializer::cleanupTasks() {
    if (gTaskManager) {
        delete gTaskManager;
        gTaskManager = nullptr;
    }
}

void SystemInitializer::cleanupModbus() {
    if (mb8art_) {
        delete mb8art_;
        mb8art_ = nullptr;
    }
    if (gModbusMaster) {
        delete gModbusMaster;
        gModbusMaster = nullptr;
    }
}

void SystemInitializer::cleanupNetwork() {
    networkConnected_ = false;
}

void SystemInitializer::cleanupHardware() {
    digitalWrite(STATUS_LED_PIN, LOW);
}
