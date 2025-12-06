// src/main.cpp
// MB8ART Full Example - Modular Architecture
//
// This example demonstrates production-ready patterns:
// - Modular initialization with SystemInitializer
// - Proper error handling with Result<T>
// - FreeRTOS task management with watchdog
// - Ethernet networking with OTA updates
// - Clean separation of concerns
//
// Hardware requirements:
// - ESP32 with LAN8720 Ethernet PHY
// - MB8ART temperature sensor module on RS485
//
// Key features:
// - Automatic device detection and initialization
// - Graceful degradation (continues without network)
// - Watchdog integration for task monitoring
// - Memory monitoring and leak detection
// - OTA firmware updates

#include <Arduino.h>
#include "config/ProjectConfig.h"
#include "init/SystemInitializer.h"

#ifdef USE_CUSTOM_LOGGER
#include <Logger.h>
#endif

#include <OTAManager.h>

static const char* TAG = "Main";

// Global system initializer (defined in SystemInitializer.cpp)
extern SystemInitializer* gSystemInitializer;

// Override Arduino loop task stack size
size_t getArduinoLoopTaskStackSize() {
    return STACK_SIZE_LOOP_TASK;
}

void setup() {
    // Initialize serial with large buffer
    Serial.setTxBufferSize(4096);
    Serial.begin(SERIAL_BAUD_RATE);

    // Wait for serial (with timeout)
    uint32_t startTime = millis();
    while (!Serial && (millis() - startTime) < 2000) {
        delay(10);
    }

    // Boot marker
    Serial.println("\n\n========== ESP32 BOOT ==========");
    Serial.flush();
    delay(100);

    // Create and run system initializer
    gSystemInitializer = new SystemInitializer();

    if (!gSystemInitializer) {
        Serial.println("FATAL: Failed to create SystemInitializer");
        while (true) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(100);
        }
    }

    auto result = gSystemInitializer->initialize();

    if (result.isError()) {
        Serial.println("FATAL: System initialization failed");
        gSystemInitializer->cleanup();

        while (true) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            delay(200);
        }
    }

    // System running - LED on
    digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
    static uint32_t lastLedToggle = 0;
    static bool ledState = true;

    // Handle OTA if network connected
    if (gSystemInitializer && gSystemInitializer->isNetworkConnected()) {
        OTAManager::handleUpdates();
    }

    // Heartbeat LED (1Hz blink when healthy)
    uint32_t now = millis();
    if (now - lastLedToggle > 1000) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState);
    }

    // Small delay to yield to other tasks
    delay(10);
}
