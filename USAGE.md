# ESP32-MB8ART Usage Guide

## Quick Start

### Basic Setup with Unified Mapping

```cpp
#include <MB8ART.h>

// Your application sensor data (Temperature_t = int16_t tenths of degrees)
struct SensorData {
    int16_t boilerOutput;       // 244 = 24.4°C
    bool isBoilerOutputValid;
    int16_t boilerReturn;
    bool isBoilerReturnValid;
    // ... etc for 8 channels
} mySensors;

void setup() {
    // 1. Create MB8ART instance (Modbus address 3)
    MB8ART mb8art(3, "MB8ART");

    // 2. Set hardware configuration (constexpr in flash - zero RAM)
    mb8art.setHardwareConfig(mb8art::DEFAULT_SENSOR_CONFIG.data());

    // 3. Bind sensor pointers
    //    Library will write directly to these variables
    std::array<mb8art::SensorBinding, 8> bindings = {{
        {&mySensors.boilerOutput, &mySensors.isBoilerOutputValid},
        {&mySensors.boilerReturn, &mySensors.isBoilerReturnValid},
        {nullptr, nullptr},  // channel 2 unused
        {nullptr, nullptr},  // channel 3 unused
        {nullptr, nullptr},  // channel 4 unused
        {nullptr, nullptr},  // channel 5 unused
        {nullptr, nullptr},  // channel 6 unused
        {nullptr, nullptr}   // channel 7 unused
    }};
    mb8art.bindSensorPointers(bindings);

    // 4. Configure channels (PT1000 sensors)
    mb8art.configureMeasurementRange(mb8art::MeasurementRange::LOW_RES);
    mb8art.configureChannelMode(0, static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT));
    mb8art.configureChannelMode(1, static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT));

    // 5. Initialize device
    auto result = mb8art.initialize();
    if (result.isError()) {
        Serial.println("MB8ART initialization failed!");
        return;
    }

    Serial.println("MB8ART ready!");
}

void loop() {
    // Request temperature readings
    mb8art.requestTemperatures();

    // Wait for data
    mb8art.waitForData(pdMS_TO_TICKS(500));

    // Temperatures automatically updated via bound pointers!
    if (mySensors.isBoilerOutputValid) {
        Serial.printf("Boiler Output: %d.%d°C\n",
                     mySensors.boilerOutput / 10,
                     abs(mySensors.boilerOutput % 10));
    }

    if (mySensors.isBoilerReturnValid) {
        Serial.printf("Boiler Return: %d.%d°C\n",
                     mySensors.boilerReturn / 10,
                     abs(mySensors.boilerReturn % 10));
    }

    delay(1000);
}
```

## Temperature Type System

### Temperature_t Format (int16_t)
- **Value**: Tenths of degrees Celsius
- **Range**: -3276.8°C to +3276.7°C
- **Examples**:
  - `244` = 24.4°C
  - `-150` = -15.0°C
  - `0` = 0.0°C

### Display Temperature
```cpp
void printTemp(int16_t temp) {
    Serial.printf("%d.%d°C\n", temp / 10, abs(temp % 10));
}
```

### Benefits of int16_t:
- No float arithmetic (faster on ESP32)
- No rounding errors
- Consistent precision (0.1°C)
- Smaller memory footprint (2 bytes vs 4 bytes)

## Hardware Configuration

### Measurement Ranges
- **LOW_RES**: -200 to 850°C, 0.1° resolution (default)
- **HIGH_RES**: -200 to 200°C, 0.01° resolution

### Channel Modes
- **PT_INPUT**: PT100/PT1000 sensors
- **THERMOCOUPLE**: J/K/T/E/R/S/B/N types
- **VOLTAGE**: ±15mV, ±50mV, ±100mV, ±1V
- **CURRENT**: 0-20mA, 4-20mA
- **DEACTIVATED**: Channel disabled

## Memory Usage

- **Hardware Config**: Lives in flash (zero RAM)
- **Runtime Bindings**: 64 bytes (8 sensors × 2 pointers × 4 bytes)
- **Internal State**: ~12 bytes per sensor

## Thread Safety

All public methods are thread-safe with mutex protection.
