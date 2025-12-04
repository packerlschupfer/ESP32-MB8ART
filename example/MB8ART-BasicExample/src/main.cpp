/**
 * @file main.cpp
 * @brief MB8ART Basic Example - Simple temperature reading demonstration
 *
 * This example demonstrates basic usage of the MB8ART library:
 * - Modbus RTU setup with correct parity (8N1)
 * - MB8ART initialization
 * - Reading temperature values from all 8 channels
 * - Checking sensor connection status
 * - Periodic temperature monitoring
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - MB8ART 8-channel temperature sensor module
 * - RS485 transceiver (e.g., MAX485)
 * - Connections: TX->DI, RX->RO, DE/RE directly driven by esp32ModbusRTU
 */

#include <Arduino.h>
#include <esp32ModbusRTU.h>
#include <ModbusRegistry.h>
#include <ModbusDevice.h>
#include <MB8ART.h>

// =============================================================================
// Configuration
// =============================================================================

// Modbus RTU pins - adjust for your hardware
#define MODBUS_RX_PIN       16      // RO pin of MAX485
#define MODBUS_TX_PIN       17      // DI pin of MAX485
#define MODBUS_BAUD_RATE    9600
#define MODBUS_CONFIG       SERIAL_8N1  // 8 data bits, No parity, 1 stop bit

// MB8ART configuration
#define MB8ART_ADDRESS      0x03    // Modbus slave address (default for MB8ART)

// Reading interval
#define READ_INTERVAL_MS    2000    // Read temperatures every 2 seconds

// =============================================================================
// Global Objects
// =============================================================================

esp32ModbusRTU modbusMaster(&Serial1);
MB8ART* tempSensor = nullptr;

// =============================================================================
// Modbus Callbacks (required for response routing)
// =============================================================================

// Forward declarations from ModbusDevice.h
extern void mainHandleData(uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                          uint16_t startingAddress, const uint8_t* data, size_t length);
extern void handleError(uint8_t serverAddress, esp32Modbus::Error error);

// =============================================================================
// Helper Functions
// =============================================================================

void printTemperatures() {
    auto result = tempSensor->getData(IDeviceInstance::DeviceDataType::TEMPERATURE);

    if (result.isOk()) {
        Serial.print("Temperatures: ");
        const auto& temps = result.value();
        for (size_t i = 0; i < temps.size() && i < 8; i++) {
            // Check if sensor is connected
            if (tempSensor->isSensorConnected(i)) {
                // Get the scale divider for this channel (10 for LOW_RES, 100 for HIGH_RES)
                float divider = tempSensor->getDataScaleDivider(
                    IDeviceInstance::DeviceDataType::TEMPERATURE, i);
                float tempC = temps[i] / divider;
                Serial.printf("T%d:%.1f°C ", i + 1, tempC);
            } else {
                Serial.printf("T%d:--- ", i + 1);
            }
        }
        Serial.println();
    } else {
        Serial.printf("Failed to read temperatures (error: %d)\n",
                     static_cast<int>(result.error()));
    }
}

void printConnectionStatus() {
    Serial.print("Sensor Status: ");
    for (int i = 0; i < 8; i++) {
        Serial.printf("S%d:%s ", i + 1,
            tempSensor->isSensorConnected(i) ? "OK" : "NC");
    }
    Serial.println();
}

// =============================================================================
// Setup
// =============================================================================

void setup() {
    // Initialize debug serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000);

    Serial.println("\n========================================");
    Serial.println("MB8ART Basic Example");
    Serial.println("8-Channel Temperature Sensor Demo");
    Serial.println("========================================\n");

    // Initialize Modbus RTU on Serial1
    Serial.println("Initializing Modbus RTU...");
    Serial1.begin(MODBUS_BAUD_RATE, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);

    // Register Modbus master with the registry (required by MB8ART)
    modbus::ModbusRegistry::getInstance().setModbusRTU(&modbusMaster);

    // Setup Modbus callbacks for response routing
    modbusMaster.onData([](uint8_t serverAddress, esp32Modbus::FunctionCode fc,
                           uint16_t address, const uint8_t* data, size_t length) {
        mainHandleData(serverAddress, fc, address, data, length);
    });

    modbusMaster.onError([](esp32Modbus::Error error) {
        handleError(0xFF, error);
    });

    // Start Modbus on core 1
    modbusMaster.begin(1);
    Serial.println("Modbus RTU initialized (9600 baud, 8N1)");

    // Create MB8ART instance
    Serial.printf("Creating MB8ART device at address 0x%02X...\n", MB8ART_ADDRESS);
    tempSensor = new MB8ART(MB8ART_ADDRESS, "MB8ART");

    // Apply default hardware configuration (stored in flash)
    tempSensor->setHardwareConfig(mb8art::DEFAULT_SENSOR_CONFIG.data());

    // Register device with Modbus registry for response routing
    modbus::ModbusRegistry::getInstance().registerDevice(MB8ART_ADDRESS, tempSensor);

    // Initialize the device
    Serial.println("Initializing MB8ART...");
    auto initResult = tempSensor->initialize();

    if (initResult.isError()) {
        Serial.printf("ERROR: MB8ART initialization failed (error: %d)\n",
                     static_cast<int>(initResult.error()));
        Serial.println("Check: wiring, power, slave address, baud rate");
        return;
    }

    // Wait for initialization to complete
    auto waitResult = tempSensor->waitForInitializationComplete(pdMS_TO_TICKS(5000));
    if (waitResult.isError()) {
        Serial.println("WARNING: Initialization wait timed out");
    }

    Serial.println("MB8ART initialized successfully!\n");

    // Print initial sensor connection status
    Serial.println("Checking sensor connections...");
    printConnectionStatus();
    Serial.println();
}

// =============================================================================
// Main Loop - Periodic Temperature Reading
// =============================================================================

void loop() {
    static unsigned long lastReadTime = 0;
    static int readCount = 0;

    if (!tempSensor || !tempSensor->isInitialized()) {
        delay(1000);
        return;
    }

    // Check if device went offline
    if (tempSensor->isModuleOffline()) {
        Serial.println("WARNING: MB8ART module is offline!");
        delay(5000);
        return;
    }

    // Read temperatures periodically
    if (millis() - lastReadTime >= READ_INTERVAL_MS) {
        lastReadTime = millis();
        readCount++;

        Serial.printf("\n--- Reading #%d ---\n", readCount);
        printTemperatures();

        // Every 10 readings, also print connection status
        if (readCount % 10 == 0) {
            printConnectionStatus();

            // Print module internal temperature if available
            float moduleTemp = tempSensor->getModuleTemperature();
            if (moduleTemp > -999.0f) {
                Serial.printf("Module Temperature: %.1f°C\n", moduleTemp);
            }
        }
    }

    // Small delay to prevent tight loop
    delay(10);
}
