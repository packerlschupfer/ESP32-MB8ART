# MB8ART

A comprehensive ESP32 library for interfacing with MB8ART 8-channel temperature acquisition modules via Modbus RTU. This library provides robust communication, automatic initialization, real-time monitoring, and extensive error handling for industrial temperature monitoring applications.

> **⚠️ Breaking Changes in v3.0.0**: Major refactoring - MB8ART.cpp split into multiple files for better maintainability. No API changes.
>
> **⚠️ Breaking Changes in v1.2.0**: Constructor signature changed and global ModbusRTU setup required. See [Migration Guide v1.2.0](MIGRATION_GUIDE_v1.2.0.md).
> 
> For v1.0.x to v1.1.0 migration, see [Migration Guide v1.1.0](MIGRATION_GUIDE_v1.1.0.md).

## Overview

The MB8ART library enables ESP32 devices to communicate with MB8ART temperature acquisition modules, which support up to 8 temperature sensors with various input types including thermocouples, PT sensors, voltage, and current inputs. The library is designed for reliability in industrial environments with features like automatic retry, connection monitoring, and event-driven updates.

## Features

- **Multi-Channel Support**: Manage up to 8 temperature channels simultaneously
- **Multiple Input Types**: 
  - Thermocouples (J, K, T, E, R, S, B, N types)
  - PT sensors (PT100, PT1000, Cu50, Cu100)
  - Voltage inputs (±15mV, ±50mV, ±100mV, ±1V)
  - Current inputs (±20mA, 4-20mA)
- **Dual Resolution Modes**:
  - Low Resolution: -200°C to 850°C with 0.1°C resolution
  - High Resolution: -200°C to 200°C with 0.01°C resolution
- **Event-Driven Architecture**: FreeRTOS event groups for efficient task synchronization
- **Automatic Initialization**: Fast device detection and configuration
- **Connection Monitoring**: Real-time channel connection status
- **Batch Operations**: Optimize communication with batch configuration reads/writes
- **Error Handling**: Comprehensive error detection and reporting
- **Thread-Safe Operations**: Mutex-protected Modbus communication
- **Flexible Logging**: Configurable debug output with performance metrics

## Hardware Requirements

- ESP32 microcontroller
- MB8ART temperature acquisition module
- RS485 transceiver for Modbus RTU communication
- Temperature sensors compatible with MB8ART (thermocouples, PT sensors, etc.)

## Dependencies

### Required
- [esp32ModbusRTU](https://github.com/bertmelis/esp32ModbusRTU) - Modbus RTU implementation
- [ModbusDevice](https://github.com/your-org/ModbusDevice) - Modbus device framework
- [MutexGuard](https://github.com/your-org/MutexGuard) - Thread-safe mutex wrapper
- FreeRTOS (included with ESP32 Arduino/IDF)
- Arduino framework for ESP32

### Optional
- [Logger](https://github.com/your-org/Logger) - Custom logging library (see [Logging Configuration](#logging-configuration))

## Project Structure

The MB8ART library is organized into multiple source files for better maintainability:

```
src/
├── MB8ART.h                 # Main header file with class declaration
├── MB8ART.cpp              # Core initialization and private helper methods
├── MB8ARTDevice.cpp        # IDeviceInstance interface implementation
├── MB8ARTModbus.cpp        # Modbus communication and response handling
├── MB8ARTState.cpp         # State query and management methods
├── MB8ARTConfig.cpp        # Configuration and settings management
├── MB8ARTSensor.cpp        # Sensor operations and data processing
├── MB8ARTEvents.cpp        # Event management and bit operations
├── MB8ARTSharedResources.h # Shared resources singleton
├── MB8ARTSharedResources.cpp # Shared resources implementation
├── MB8ARTLoggingMacros.h  # Logging macro definitions
├── CommonModbusDefinitions.h # Common Modbus type definitions
└── TemperatureControlModule.cpp # Temperature control module (optional)
```

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = 
    MB8ART
    esp32ModbusRTU=https://github.com/bertmelis/esp32ModbusRTU.git

build_flags = 
    -DMB8ART_DEBUG  ; Optional: Enable debug logging
```

#### With Custom Logger
If you want to use the custom Logger library:

```ini
lib_deps = 
    MB8ART
    esp32ModbusRTU=https://github.com/bertmelis/esp32ModbusRTU.git
    Logger  ; Add Logger library

build_flags = 
    -DMB8ART_USE_CUSTOM_LOGGER  ; Enable custom logger
    -DMB8ART_DEBUG              ; Optional: Enable debug logging
```

### Arduino IDE

1. Download the library
2. Place in your Arduino libraries folder
3. Install dependencies via Library Manager

## Basic Usage

```cpp
#include <MB8ART.h>
#include <esp32ModbusRTU.h>
#include <ModbusDevice.h>  // For setGlobalModbusRTU

// Create Modbus RTU instance
esp32ModbusRTU modbus(&Serial2, 16);  // GPIO16 for DE/RE control

// Create MB8ART instance
MB8ART tempModule(0x01, "TempModule1");  // Address 0x01

void setup() {
    Serial.begin(115200);
    
    // Initialize Modbus
    Serial2.begin(9600, SERIAL_8N1, 26, 27);  // RX=26, TX=27
    
    // Set global ModbusRTU instance (required)
    setGlobalModbusRTU(&modbus);
    
    // Set up callbacks
    modbus.onData(mainHandleData);
    modbus.onError(handleError);
    
    modbus.begin();
    
    // Initialize MB8ART module
    tempModule.initialize();
    
    // Wait for initialization
    if (tempModule.waitForInitializationComplete(5000)) {
        Serial.println("MB8ART initialized successfully");
        
        // Configure channels (example: all as K-type thermocouples)
        tempModule.configureAllChannels(
            mb8art::ChannelMode::THERMOCOUPLE,
            static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_K)
        );
    }
}

void loop() {
    // Request temperature data
    if (tempModule.requestData()) {
        // Wait for data
        if (tempModule.waitForData()) {
            // Process the data
            tempModule.processData();
            
            // Read temperatures
            for (int i = 0; i < 8; i++) {
                float temp = tempModule.getSensorTemperature(i);
                if (tempModule.getSensorConnectionStatus(i)) {
                    Serial.printf("Channel %d: %.2f°C\n", i, temp);
                }
            }
        }
    }
    
    delay(1000);
}
```

## Advanced Usage

### Event-Driven Operation with FreeRTOS

```cpp
// Task for temperature monitoring
void temperatureTask(void* pvParameters) {
    MB8ART* tempModule = (MB8ART*)pvParameters;
    EventGroupHandle_t eventGroup = tempModule->getEventGroup();
    
    while (true) {
        // Request data
        tempModule->requestData();
        
        // Wait for data ready event
        EventBits_t bits = xEventGroupWaitBits(
            eventGroup,
            MB8ART::DATA_READY_BIT | MB8ART::DATA_ERROR_BIT,
            pdTRUE,    // Clear bits on exit
            pdFALSE,   // Wait for any bit
            pdMS_TO_TICKS(1000)
        );
        
        if (bits & MB8ART::DATA_READY_BIT) {
            tempModule->processData();
            
            // Check for sensor updates
            for (int i = 0; i < 8; i++) {
                if (bits & (mb8art::toUnderlyingType(mb8art::getSensorUpdateBit(i)) << 8)) {
                    float temp = tempModule->getSensorTemperature(i);
                    Serial.printf("Sensor %d updated: %.2f°C\n", i, temp);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

### Channel Configuration

```cpp
// Configure individual channel
tempModule.configureChannelMode(0, 
    static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT) |
    (static_cast<uint16_t>(mb8art::PTType::PT100) << 8)
);

// Configure range of channels
tempModule.configureChannelRange(0, 3,  // Channels 0-3
    mb8art::ChannelMode::VOLTAGE,
    static_cast<uint16_t>(mb8art::VoltageRange::MV_50)
);

// Set measurement range
tempModule.configureMeasurementRange(mb8art::MeasurementRange::HIGH_RES);
```

### Connection Monitoring

```cpp
// Check module responsiveness
if (tempModule.isModuleResponsive()) {
    // Get connected channels
    std::vector<uint8_t> connected = tempModule.getConnectedChannels();
    Serial.printf("Connected channels: %d\n", connected.size());
    
    // Print diagnostics
    tempModule.printChannelDiagnostics();
}
```

### Callback Registration

```cpp
// Register temperature callback
tempModule.registerTemperatureCallback(
    [](const mb8art::SensorReading readings[], size_t size) {
        for (size_t i = 0; i < size; i++) {
            if (readings[i].isTemperatureValid) {
                Serial.printf("Ch%d: %.2f°C\n", i, readings[i].temperature);
            }
        }
    }
);

// Register Modbus response callback
tempModule.registerModbusResponseCallback(
    [](uint8_t functionCode, uint8_t* data, uint16_t length) {
        Serial.printf("Modbus response: FC=%d, Len=%d\n", functionCode, length);
    }
);
```

## API Reference

### Core Methods

#### Initialization
- `initialize()` - Start initialization sequence
- `isInitialized()` - Check basic initialization status
- `isInitializationComplete()` - Check full initialization completion
- `waitForInitializationComplete(timeout)` - Wait for initialization with timeout

#### Data Operations
- `requestData()` - Request temperature readings from all channels
- `waitForData()` - Wait for data to arrive
- `processData()` - Process received data and update readings
- `getData(dataType)` - Get specific data type

#### Configuration
- `configureChannelMode(channel, mode)` - Configure single channel
- `configureAllChannels(mode, subType)` - Configure all channels
- `configureChannelRange(start, end, mode, subType)` - Configure channel range
- `configureMeasurementRange(range)` - Set temperature resolution

#### Status and Monitoring
- `isModuleResponsive()` - Check if module is communicating
- `getSensorConnectionStatus(channel)` - Check if sensor is connected
- `getActiveChannelCount()` - Get number of active channels
- `getConnectedChannels()` - Get list of connected channels

#### Data Access
- `getSensorTemperature(channel)` - Get temperature for specific channel
- `getSensorReading(channel)` - Get full sensor reading struct
- `getAllSensorReadings(destination)` - Copy all readings to array

### Event Bits

The library uses FreeRTOS event groups for task synchronization:

- `DATA_REQUEST_BIT` - Data request initiated
- `DATA_READY_BIT` - Data received and ready
- `DATA_ERROR_BIT` - Error occurred during operation
- `INIT_COMPLETE_BIT` - Initialization complete
- `SENSOR1_UPDATE_BIT` through `SENSOR8_UPDATE_BIT` - Individual sensor updates
- `SENSOR1_ERROR_BIT` through `SENSOR8_ERROR_BIT` - Individual sensor errors

### Data Structures

```cpp
struct SensorReading {
    float temperature;              // Temperature value
    bool isTemperatureValid;        // Validity flag
    bool Error;                     // Error flag
    TickType_t lastTemperatureUpdated;  // Last update timestamp
    bool lastCommandSuccess;        // Last command status
    bool isStateConfirmed;          // State confirmation
};

struct ChannelConfig {
    uint16_t mode;     // Channel mode (THERMOCOUPLE, PT_INPUT, etc.)
    uint16_t subType;  // Subtype (TYPE_K, PT100, etc.)
};
```

## Logging Configuration

MB8ART supports flexible logging configuration with both ESP-IDF and custom Logger backends.

### Using ESP-IDF Logging (Default)
No configuration needed. The library will use ESP-IDF logging:

```cpp
#include <MB8ART.h>
// Logs appear as: I (1234) MB8ART: Message
```

### Using Custom Logger
Define `USE_CUSTOM_LOGGER` in your build flags:

```ini
# platformio.ini
build_flags = -DUSE_CUSTOM_LOGGER

lib_deps = 
    Logger  # Must include Logger when using custom logger
    MB8ART
```

Your application must include LogInterfaceImpl.h:
```cpp
#include <Logger.h>
#include <LogInterfaceImpl.h>  // Include once in main application
#include <MB8ART.h>

void setup() {
    Logger::getInstance().init(1024);
    // MB8ART will automatically use custom Logger
}
```

### Debug Logging
To enable debug/verbose logging for MB8ART:

```ini
build_flags = -DMB8ART_DEBUG
```

This enables:
- Debug level (D) and Verbose level (V) logging
- Performance timing macros
- Buffer dump utilities
- Protocol-level debugging

### Complete Example
```ini
[env:debug]
build_flags = 
    -DUSE_CUSTOM_LOGGER  ; Use custom logger
    -DMB8ART_DEBUG       ; Enable debug for MB8ART
```

### Log Levels
- **ERROR**: Critical failures (always visible)
- **WARN**: Important issues (always visible)  
- **INFO**: Major state changes (always visible)
- **DEBUG**: Detailed operation info (only with MB8ART_DEBUG)
- **VERBOSE**: Very detailed traces (only with MB8ART_DEBUG)

## Configuration

### Build Flags

```cpp
// Timing configuration
#define MB8ART_MIN_REQUEST_INTERVAL_MS 25   // Minimum time between requests
#define MB8ART_REQUEST_TIMEOUT_MS 500       // Modbus request timeout
#define MB8ART_INTER_REQUEST_DELAY_MS 5     // Delay between requests
#define MB8ART_RETRY_COUNT 3                // Number of retries

// Debug options
#define MB8ART_DEBUG                        // Enable debug logging
#define MB8ART_DEBUG_FULL                   // Enable verbose logging
```

### Module Settings

The library automatically reads module settings during initialization:
- RS485 address
- Baud rate
- Parity settings
- Module temperature
- Measurement range

## Performance Considerations

1. **Batch Operations**: Use batch configuration methods to minimize Modbus traffic
2. **Event-Driven Design**: Use event groups instead of polling for better efficiency
3. **Connection Monitoring**: The library tracks connection status to avoid reading disconnected channels
4. **Timing Constraints**: Respect minimum request intervals to avoid overwhelming the module

## Troubleshooting

### Common Issues

1. **Initialization Fails**
   - Check RS485 wiring and termination
   - Verify module address matches code
   - Ensure proper baud rate and parity settings

2. **No Temperature Readings**
   - Verify sensors are properly connected
   - Check channel configuration matches sensor type
   - Ensure measurement range is appropriate

3. **Communication Errors**
   - Check RS485 bias and termination resistors
   - Verify cable length and quality
   - Monitor for electrical noise

### Debug Output

Enable debug logging to see detailed operation:

```cpp
#define MB8ART_DEBUG
#define ESP32MODBUSRTU_DEBUG
```

## Example Projects

### FreeRTOS Task Manager Integration

The included example in `example/ESPlan-blueprint-libs-freertos-taskmanager-MB8ART-workspace` demonstrates advanced usage with FreeRTOS task management:

```cpp
#include <Logger.h>
#include <TaskManager.h>
#include <MB8ART.h>

// Global instances
Logger logger;
TaskManager taskManager;
MB8ART* sensorInstance;
esp32ModbusRTU modbusMaster(&Serial1);

// Shared data structure for thread-safe sensor readings
struct SharedSensorReadings {
    float temperature;
    float resistance;
    bool isTemperatureValid;
    bool isResistanceValid;
    int error;
};

SharedSensorReadings sharedSensorReadings;
SemaphoreHandle_t sensorInstanceMutex;
QueueHandle_t sensorQueue;

// Temperature request task using event groups
void TempReqTask(void* pvParameters) {
    MB8ART* sensorInstance = static_cast<MB8ART*>(pvParameters);
    TickType_t xTicksToWait = pdMS_TO_TICKS(2000);
    
    while (1) {
        if (xSemaphoreTake(sensorInstanceMutex, portMAX_DELAY)) {
            EventBits_t uxBits = xEventGroupWaitBits(
                sensorInstance->getEventGroup(),
                TIMER_TRIGGER_BIT,
                pdTRUE, 
                pdFALSE, 
                xTicksToWait);
            
            xSemaphoreGive(sensorInstanceMutex);
            
            if ((uxBits & TIMER_TRIGGER_BIT) != 0) {
                // Timer triggered - request temperature data
                sensorInstance->reqTemperatures();
            }
        }
    }
}

// Sensor reading task with queued data processing
void SensorReadTask(void* pvParameters) {
    for (;;) {
        if (xSemaphoreTake(sensorInstanceMutex, portMAX_DELAY)) {
            SensorReading readings[DEFAULT_NUMBER_OF_SENSORS];
            sensorInstance->getAllSensorReadings(readings);
            xSemaphoreGive(sensorInstanceMutex);
            
            // Send data to processing queue
            xQueueSend(sensorQueue, readings, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100000)); // 100-second cycle
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize logger and task manager
    logger.init(512);
    logger.enableLogging(true);
    logger.setLogLevel(ESP_LOG_DEBUG);
    
    // Start debug task
    taskManager.startTask(&TaskManager::debugTask, "DebugTask", 8096, 5, "DBG");
    
    // Initialize Serial1 for RS485
    Serial1.begin(9600, SERIAL_8N1, 36, 4); // RX=36, TX=4
    
    // Set global ModbusRTU instance (required for ModbusDevice v2.0.0+)
    setGlobalModbusRTU(&modbusMaster);
    
    // Set up callbacks
    modbusMaster.onData(mainHandleData);
    modbusMaster.onError(handleError);
    
    modbusMaster.begin();
    
    // Create mutexes and queues
    sensorInstanceMutex = xSemaphoreCreateMutex();
    sensorQueue = xQueueCreate(10, sizeof(SensorReading) * DEFAULT_NUMBER_OF_SENSORS);
    
    // Initialize MB8ART instance
    if (xSemaphoreTake(sensorInstanceMutex, portMAX_DELAY)) {
        sensorInstance = new MB8ART(0x01, "PTA8C04C");
        // Note: Device registration happens automatically during initialization
        
        // Start temperature request task
        taskManager.startTask(TempReqTask, "TempReqTask", 15000, sensorInstance, 2, "Trq");
        taskManager.startTask(SensorReadTask, "SensorReadTask", 13000, 2, "SRe");
        
        xSemaphoreGive(sensorInstanceMutex);
    }
}

void loop() {
    if (sensorInstance != nullptr) {
        // Print module settings
        const ModuleSettings& settings = sensorInstance->getModuleSettings();
        sensorInstance->printModuleSettings(settings);
        
        // Request and print temperatures
        sensorInstance->reqTemperatures(DEFAULT_NUMBER_OF_SENSORS);
        const SensorReading* readings = sensorInstance->getSensorReadings();
        
        for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            sensorInstance->printSensorReading(readings[i], i);
        }
    }
    delay(15000);
}
```

This example demonstrates:
- **Thread-safe sensor access** using mutexes
- **Event-driven temperature requests** with FreeRTOS event groups
- **Task-based architecture** with TaskManager integration
- **Queued data processing** for real-time applications
- **Resource management** with proper initialization and cleanup

See the `examples` folder for complete projects demonstrating:
- Basic temperature monitoring
- Multi-module systems
- MQTT integration  
- Industrial control applications

## Recent Optimizations

### v2.0 Performance Improvements
- **Batch Configuration**: Configure all 8 channels in ~30ms (vs ~160ms with individual writes)
- **Batch Reading**: Read all configurations in 2 requests during initialization (vs 16+ individual requests)
- **Connection Status Caching**: 5-second cache reduces Modbus traffic by ~90%
- **Optimized Data Freshness**: O(1) checking with global timestamp tracking
- **Memory Optimization**: ~44 bytes saved through bit fields and structure packing
- **Passive Responsiveness**: No polling needed - tracks last response time from any operation
- **Pre-computed Channel Masks**: Eliminates repeated calculations in hot paths

See [CHANGELOG.md](CHANGELOG.md) for complete version history.

## Polling Behavior

MB8ART uses a **request-response model** with no background polling. Key characteristics:

- **No Automatic Polling**: The library never initiates communication without explicit application requests
- **Offline Device Protection**: All request methods (`req*`, `request*`, `configure*`) check device status and return early if offline
- **Error Prevention**: When initialization fails or device goes offline, all polling attempts are blocked at the method level
- **Application Control**: Temperature updates only occur when the application calls `requestTemperatures()` or similar methods
- **Event-Driven Updates**: Use FreeRTOS event groups to signal when new data arrives after a request

### Example: Preventing Unwanted Polling

```cpp
// After initialization failure, all these calls return immediately without Modbus communication:
if (!tempModule.isInitialized() || tempModule.isModuleOffline()) {
    // These methods detect offline state and return false/error without polling:
    tempModule.requestTemperatures();        // Returns false
    tempModule.reqMeasurementRange();        // Returns false  
    tempModule.refreshConnectionStatus();    // Returns false
    tempModule.configureChannelMode(0, 0);  // Returns COMMUNICATION_ERROR
}
```

This design prevents the "periodic 5-second Modbus polling" issue observed when devices fail initialization but continue receiving request attempts.

## License

This library is released under the MIT License. See LICENSE file for details.

## Contributing

Contributions are welcome! Please submit pull requests with:
- Clear description of changes
- Test results
- Documentation updates

## Support

For issues and feature requests, please use the GitHub issue tracker.