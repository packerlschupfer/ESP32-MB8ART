# MB8ART Usage Example

This document provides example code for using the MB8ART 8-channel temperature module in your project.

## Basic Setup

```cpp
#include "MB8ART.h"
#include <ModbusRegistry.h>
#include <ModbusDevice.h>
#include <esp32ModbusRTU.h>

// Global Modbus instance
esp32ModbusRTU modbus(&Serial2, 16);  // GPIO16 for DE/RE pin

// MB8ART device instance
MB8ART* temperatureModule = nullptr;

void setup() {
    Serial.begin(115200);
    
    // Configure Modbus RTU
    Serial2.begin(9600, SERIAL_8N1, 17, 18);  // RX=17, TX=18
    modbus.begin();
    
    // Register Modbus master with the registry
    modbus::ModbusRegistry::getInstance().setModbusRTU(&modbus);

    // Register Modbus callbacks
    modbus.onData(mainHandleData);
    modbus.onError(handleError);

    // Create MB8ART device
    uint8_t deviceAddress = 0x01;  // Your device's Modbus address
    temperatureModule = new MB8ART(deviceAddress, "MB8ART");

    // Register device with Modbus registry for response routing
    modbus::ModbusRegistry::getInstance().registerDevice(deviceAddress, temperatureModule);
    
    // Initialize the device
    auto result = temperatureModule->initialize();
    if (result.isOk()) {
        Serial.println("MB8ART initialized successfully");
    } else {
        Serial.println("MB8ART initialization failed");
    }
}
```

## Reading Temperature Data

```cpp
void readTemperatures() {
    // Check if device is offline
    if (temperatureModule->isModuleOffline()) {
        Serial.println("MB8ART module is offline");
        return;
    }
    
    // Request temperature data from all 8 channels
    auto result = temperatureModule->reqTemperatures(8, false);  // false = low resolution
    if (!result.isOk()) {
        Serial.println("Failed to request temperatures");
        return;
    }
    
    // Wait for data (with timeout)
    if (temperatureModule->waitForData()) {
        // Process the data
        temperatureModule->processData();
        
        // Read individual sensor values
        for (int i = 0; i < 8; i++) {
            float temp = temperatureModule->getSensorTemperature(i);
            bool isValid = temperatureModule->wasSensorLastCommandSuccessful(i);
            
            if (isValid) {
                Serial.printf("Channel %d: %.1f°C\n", i + 1, temp);
            } else {
                Serial.printf("Channel %d: No sensor connected\n", i + 1);
            }
        }
    } else {
        Serial.println("Timeout waiting for temperature data");
    }
}
```

## Configuring Channels

```cpp
void configureChannels() {
    // Configure all channels as J-type thermocouples
    auto result = temperatureModule->configureAllChannels(
        mb8art::ChannelMode::THERMOCOUPLE,
        static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_J)
    );
    
    if (result == mb8art::SensorErrorCode::SUCCESS) {
        Serial.println("All channels configured as J-type thermocouples");
    }
    
    // Or configure individual channels
    temperatureModule->configureChannelMode(0, 
        static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT) | 
        (static_cast<uint16_t>(mb8art::PTType::PT100) << 8)
    );
}
```

## Using Callbacks

```cpp
void onTemperatureData(const MB8ART::SensorReading readings[], size_t size) {
    Serial.println("Temperature data received:");
    for (size_t i = 0; i < size; i++) {
        if (readings[i].isTemperatureValid) {
            Serial.printf("  Sensor %d: %.2f°C\n", i + 1, readings[i].temperature);
        }
    }
}

void setup() {
    // ... other setup code ...
    
    // Register temperature callback
    temperatureModule->registerTemperatureCallback(onTemperatureData);
}
```

## Handling Device Offline

```cpp
void checkDeviceStatus() {
    // Check if device is responsive
    if (!temperatureModule->isModuleResponsive()) {
        Serial.println("MB8ART not responding - may be offline");
        
        // Try to probe the device
        if (!temperatureModule->probeDevice()) {
            Serial.println("Device probe failed - device is offline");
            // Handle offline state (e.g., retry later, alert user)
        }
    }
}
```

## Task-Based Example

```cpp
void temperatureTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2000);  // Read every 2 seconds
    
    while (true) {
        // Check if device is online
        if (!temperatureModule->isModuleOffline()) {
            // Request temperature data
            auto result = temperatureModule->reqTemperatures();
            
            if (result.isOk() && temperatureModule->waitForData()) {
                temperatureModule->processData();
                
                // Data is now available via getSensorTemperature()
                // You can also use event groups to notify other tasks
                EventGroupHandle_t eventGroup = temperatureModule->getEventGroup();
                EventBits_t bits = xEventGroupGetBits(eventGroup);
                
                if (bits & MB8ART::DATA_READY_BIT) {
                    // Process the data...
                }
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    // ... initialization code ...
    
    // Create temperature reading task
    xTaskCreate(temperatureTask, "TempTask", 4096, NULL, 1, NULL);
}
```

## Error Handling

```cpp
void handleModbusErrors() {
    // Get device statistics
    auto stats = temperatureModule->getStatistics();
    
    Serial.printf("Total requests: %d\n", stats.totalRequests);
    Serial.printf("Successful: %d\n", stats.successfulRequests);
    Serial.printf("Failed: %d\n", stats.failedRequests);
    Serial.printf("Success rate: %.1f%%\n", stats.successRate);
    
    // Check connection state
    switch (stats.state) {
        case ModbusDevice::ConnectionState::CONNECTED:
            Serial.println("Device connected");
            break;
        case ModbusDevice::ConnectionState::ERROR:
            Serial.println("Device in error state");
            // Consider resetting or reinitializing
            break;
        default:
            break;
    }
}
```

## Complete Working Example

See the `src/main.cpp` file in this example project for a complete working implementation that demonstrates:
- Device initialization with proper error handling
- Periodic temperature reading
- Channel configuration
- Offline device handling
- Integration with FreeRTOS tasks

## Troubleshooting

1. **Device not responding**: Check wiring, RS485 termination, and device address
2. **Initialization timeout**: Ensure device is powered and RS485 bus is properly configured
3. **Invalid readings**: Verify sensor connections and channel configuration
4. **Intermittent communication**: Check for EMI, proper grounding, and cable quality

For more details, refer to the MB8ART hardware documentation and Modbus register map.