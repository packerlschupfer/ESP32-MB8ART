// MB8ARTOptimizationDemo.cpp
//
// This file demonstrates the optimizations and new features added to MB8ART library
//

#include "MB8ART.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char* TAG = "MB8ART_DEMO";

// Function to demonstrate batch channel configuration
void demonstrateBatchConfiguration(MB8ART* device) {
    ESP_LOGI(TAG, "\n=== Batch Configuration Demo ===");
    
    // Configure all channels to PT1000 in one batch operation
    ESP_LOGI(TAG, "Configuring all 8 channels to PT1000 mode...");
    TickType_t startTime = xTaskGetTickCount();
    auto result = device->configureAllChannels(mb8art::ChannelMode::PT_INPUT, 
                                               static_cast<uint16_t>(mb8art::PTType::PT1000));
    TickType_t elapsedTime = xTaskGetTickCount() - startTime;
    
    if (result.isOk()) {
        ESP_LOGI(TAG, "✓ All channels configured successfully in single transaction!");
        ESP_LOGI(TAG, "  Time: %lu ms (vs ~160ms with individual writes)", pdTICKS_TO_MS(elapsedTime));
    } else {
        ESP_LOGE(TAG, "✗ Batch configuration failed");
    }
    
    // Configure specific range of channels
    ESP_LOGI(TAG, "\nConfiguring channels 4-7 to thermocouple type K...");
    startTime = xTaskGetTickCount();
    result = device->configureChannelRange(4, 7, 
                                          mb8art::ChannelMode::THERMOCOUPLE,
                                          static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_K));
    elapsedTime = xTaskGetTickCount() - startTime;
    
    if (result.isOk()) {
        ESP_LOGI(TAG, "✓ Channels 4-7 reconfigured successfully!");
        ESP_LOGI(TAG, "  Time: %lu ms (vs ~80ms with individual writes)", pdTICKS_TO_MS(elapsedTime));
    }
    
    // Set measurement range
    ESP_LOGI(TAG, "\nSetting high resolution mode...");
    result = device->configureMeasurementRange(mb8art::MeasurementRange::HIGH_RES);
    
    if (result.isOk()) {
        ESP_LOGI(TAG, "✓ High resolution mode enabled (-200 to 200°C, 0.01° resolution)");
    }
}

// Function to demonstrate connection status caching
void demonstrateConnectionStatusCaching(MB8ART* device) {
    ESP_LOGI(TAG, "\n=== Connection Status Caching Demo ===");
    
    // First call - will fetch from device
    ESP_LOGI(TAG, "First connection status check (fetches from device)...");
    TickType_t startTime = xTaskGetTickCount();
    bool success = device->refreshConnectionStatus();
    TickType_t elapsed = xTaskGetTickCount() - startTime;
    ESP_LOGI(TAG, "Result: %s, Time: %lu ms", success ? "Success" : "Failed", 
             pdTICKS_TO_MS(elapsed));
    
    // Immediate second call - should use cache
    ESP_LOGI(TAG, "\nSecond connection status check (should use cache)...");
    startTime = xTaskGetTickCount();
    success = device->refreshConnectionStatus();
    elapsed = xTaskGetTickCount() - startTime;
    ESP_LOGI(TAG, "Result: %s, Time: %lu ms (cached!)", success ? "Success" : "Failed", 
             pdTICKS_TO_MS(elapsed));
    
    // Check individual channels
    ESP_LOGI(TAG, "\nChannel connection status:");
    for (int i = 0; i < 8; i++) {
        bool connected = device->getSensorConnectionStatus(i);
        ESP_LOGI(TAG, "  Channel %d: %s", i, connected ? "Connected" : "Disconnected");
    }
}

// Function to demonstrate data freshness check optimization
void demonstrateDataFreshnessCheck(MB8ART* device) {
    ESP_LOGI(TAG, "\n=== Data Freshness Check Demo ===");
    
    // Request fresh data
    ESP_LOGI(TAG, "Requesting temperature data...");
    auto result = device->reqTemperatures();
    if (result.isOk() && device->waitForData()) {
        ESP_LOGI(TAG, "✓ Fresh data received");
        
        // Now demonstrate the optimized hasRecentSensorData check
        const TickType_t testIntervals[] = {1000, 2000, 5000, 10000}; // ms
        
        for (auto interval : testIntervals) {
            TickType_t startTime = xTaskGetTickCount();
            bool hasRecent = device->hasRecentSensorData(interval);
            TickType_t elapsed = xTaskGetTickCount() - startTime;
            
            ESP_LOGI(TAG, "Data within %lu ms? %s (check took %lu ticks)", 
                     interval, hasRecent ? "YES" : "NO", elapsed);
        }
        
        // Wait and check again
        ESP_LOGI(TAG, "\nWaiting 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        ESP_LOGI(TAG, "Checking data freshness again:");
        bool fresh2s = device->hasRecentSensorData(2000);
        bool fresh5s = device->hasRecentSensorData(5000);
        ESP_LOGI(TAG, "  Within 2s: %s", fresh2s ? "YES" : "NO");
        ESP_LOGI(TAG, "  Within 5s: %s", fresh5s ? "YES" : "NO");
    }
}

// Function to demonstrate memory optimization results
void demonstrateMemoryOptimization(MB8ART* device) {
    ESP_LOGI(TAG, "\n=== Memory Optimization Demo ===");
    
    // Show structure sizes
    ESP_LOGI(TAG, "Structure sizes after optimization:");
    ESP_LOGI(TAG, "  SensorReading: %zu bytes (was ~16 bytes)", sizeof(mb8art::SensorReading));
    ESP_LOGI(TAG, "  - Uses bit fields for 4 boolean flags");
    ESP_LOGI(TAG, "  - Saves 3 bytes per sensor reading");
    
    ESP_LOGI(TAG, "\nMB8ART internal optimizations:");
    ESP_LOGI(TAG, "  - statusFlags: 1 byte (was 3 separate bools)");
    ESP_LOGI(TAG, "  - sensorConnected: 1 byte for 8 sensors (was 8 bytes)");
    ESP_LOGI(TAG, "  - Pre-computed channel mask eliminates repeated calculations");
    
    // Access sensor data to show bit field usage
    for (int i = 0; i < 3; i++) {
        auto reading = device->getSensorReading(i);
        ESP_LOGI(TAG, "\nChannel %d status (bit fields):", i);
        ESP_LOGI(TAG, "  isTemperatureValid: %d", reading.isTemperatureValid);
        ESP_LOGI(TAG, "  Error: %d", reading.Error);
        ESP_LOGI(TAG, "  lastCommandSuccess: %d", reading.lastCommandSuccess);
        ESP_LOGI(TAG, "  isStateConfirmed: %d", reading.isStateConfirmed);
    }
}

// Function to demonstrate passive responsiveness monitoring
void demonstratePassiveResponsiveness(MB8ART* device) {
    ESP_LOGI(TAG, "\n=== Passive Responsiveness Demo ===");
    
    // Check responsiveness multiple times
    for (int i = 0; i < 3; i++) {
        TickType_t startTime = xTaskGetTickCount();
        bool responsive = device->isModuleResponsive();
        TickType_t elapsed = xTaskGetTickCount() - startTime;
        
        ESP_LOGI(TAG, "Check %d: Module %s (took %lu ticks)", 
                 i + 1, responsive ? "RESPONSIVE" : "NOT RESPONSIVE", elapsed);
        
        if (i == 0) {
            ESP_LOGI(TAG, "  (First check may have triggered actual polling)");
        } else {
            ESP_LOGI(TAG, "  (Should use passive monitoring - no polling)");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Request data to update lastResponseTime
    ESP_LOGI(TAG, "\nRequesting data to update response timestamp...");
    device->reqTemperatures();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Check again - should be instant
    TickType_t startTime = xTaskGetTickCount();
    bool responsive = device->isModuleResponsive();
    TickType_t elapsed = xTaskGetTickCount() - startTime;
    ESP_LOGI(TAG, "After data request: %s (took %lu ticks - passive check!)", 
             responsive ? "RESPONSIVE" : "NOT RESPONSIVE", elapsed);
}

// Main demo function to be called from your application
void runMB8ARTOptimizationDemo(MB8ART* device) {
    if (!device || !device->isInitialized()) {
        ESP_LOGE(TAG, "MB8ART device not initialized!");
        return;
    }
    
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     MB8ART Optimization Demo               ║");
    ESP_LOGI(TAG, "║     Showcasing v2.0 Improvements           ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝");
    
    // Run each demonstration
    demonstrateBatchConfiguration(device);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    demonstrateConnectionStatusCaching(device);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    demonstrateDataFreshnessCheck(device);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    demonstrateMemoryOptimization(device);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    demonstratePassiveResponsiveness(device);
    
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     Demo Complete!                         ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝");
}