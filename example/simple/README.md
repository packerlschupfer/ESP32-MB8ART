# MB8ART Temperature Monitoring System Example

A modern, production-ready example demonstrating the MB8ART 8-channel temperature acquisition module with ESP32 using FreeRTOS task-based architecture.

## 🌟 Features

### Architecture
- **Modern FreeRTOS Design**: Task-based architecture with proper separation of concerns
- **ModbusDevice Framework**: Leverages the new unified Modbus architecture
- **Three-Tier Logging**: Release, Debug Selective, and Debug Full modes
- **Watchdog Protection**: Task-level watchdog monitoring
- **Error Recovery**: Automatic device offline detection and recovery

### Functionality
- **Multi-Channel Temperature Monitoring**: Support for all 8 channels
- **Flexible Sensor Support**: Thermocouples, PT100/1000, voltage, current inputs
- **WiFi/OTA Updates**: Remote firmware updates via ArduinoOTA
- **Real-time Monitoring**: System health, network status, and temperature statistics
- **Alarm System**: Temperature threshold monitoring (placeholder for expansion)
- **Data Processing**: Export, logging, and analysis capabilities (placeholder)

## 📋 Hardware Requirements

- ESP32 development board (ESP32-WROOM-32 or similar)
- RS485 transceiver module (MAX485 or similar)
- MB8ART 8-channel temperature module
- Temperature sensors (based on your needs):
  - Thermocouples (J, K, T, E, R, S, B, N types)
  - PT100/PT1000 RTD sensors
  - Voltage sources (±15mV to ±1V)
  - Current sources (0-20mA, 4-20mA)
- 5V power supply for MB8ART module

## 🔌 Wiring Diagram

### RS485 Connection
```
ESP32         RS485 Module    MB8ART Module
---------     ------------    -------------
GPIO17 (RX) → RO              
GPIO18 (TX) → DI              
GPIO16      → DE/RE           
GND         → GND           → GND
5V/3.3V     → VCC           
              A+ ←----------→ A+ (RS485+)
              B- ←----------→ B- (RS485-)
                             
                             120Ω termination
                             resistor on both
                             ends of RS485 bus
```

### Pin Configuration
| Function | Default GPIO | Configurable in |
|----------|--------------|-----------------|
| RS485 RX | GPIO17 | `src/config/ProjectConfig.h` |
| RS485 TX | GPIO18 | `src/config/ProjectConfig.h` |
| DE/RE Control | GPIO16 | `src/config/ProjectConfig.h` |
| Status LED | GPIO2 | `src/config/ProjectConfig.h` |

## 🚀 Quick Start

### 1. Environment Setup
```bash
# Clone or download the example
cd MB8ART/example/ESPlan-blueprint-libs-freertos-taskmanager-MB8ART-workspace

# Set WiFi credentials (for OTA updates)
export WIFI_SSID="your-wifi-ssid"
export WIFI_PASSWORD="your-wifi-password"
```

### 2. Configure Your Setup
Edit `src/config/ProjectConfig.h`:
```cpp
// Adjust pin configuration if needed
#define MODBUS_RX_PIN 17
#define MODBUS_TX_PIN 18

// Set your MB8ART address
#define MB8ART_ADDRESS 0x01

// Configure temperature units
#define DEFAULT_TEMPERATURE_UNIT "C"  // or "F"
```

### 3. Build and Upload

#### With Custom Logger (Default)
```bash
# For production use with Logger
pio run -e mb8art_release -t upload

# For debugging with selective output
pio run -e mb8art_debug_selective -t upload

# For troubleshooting with maximum verbosity
pio run -e mb8art_debug_full -t upload

# With Logger enabled in all libraries
pio run -e mb8art_custom_logger -t upload
```

#### Without Logger Dependency
```bash
# Build without Logger library (uses ESP-IDF logging)
pio run -e mb8art_no_logger -t upload
```

#### Monitor Output
```bash
# Monitor serial output
pio device monitor
```

## 📊 Build Environments

| Environment | Purpose | Logging System | Logging Level | Optimization |
|-------------|---------|----------------|---------------|--------------|
| `mb8art_release` | Production deployment | Custom Logger | INFO only | -O2 |
| `mb8art_debug_selective` | Development/testing | Custom Logger | Strategic DEBUG | -O0 |
| `mb8art_debug_full` | Troubleshooting | Custom Logger | All DEBUG/VERBOSE | -O0 |
| `mb8art_custom_logger` | Custom Logger demo | Custom Logger (all libs) | Configurable | -O0 |
| `mb8art_no_logger` | No Logger dependency | ESP-IDF only | INFO only | -O2 |
| `mb8art_test` | Unit testing | Custom Logger | Full DEBUG | -O0 |

### Choosing the Right Environment

- **For production**: Use `mb8art_release` (with Logger) or `mb8art_no_logger` (without Logger)
- **For development**: Use `mb8art_debug_selective` for balanced debug output
- **For troubleshooting**: Use `mb8art_debug_full` for maximum verbosity
- **For library integration**: Use `mb8art_custom_logger` to enable Logger in all libraries
- **For minimal dependencies**: Use `mb8art_no_logger` - relies only on ESP-IDF logging

## 🏗️ Project Structure

```
├── src/
│   ├── config/
│   │   └── ProjectConfig.h      # Central configuration
│   ├── tasks/
│   │   ├── TemperatureTask.*    # Temperature acquisition
│   │   ├── MonitoringTask.*     # System monitoring
│   │   ├── DataProcessingTask.h # Data export/logging
│   │   ├── AlarmTask.h          # Threshold monitoring
│   │   └── OTATask.h            # OTA update handling
│   ├── utils/
│   │   └── WatchdogHelper.h     # RAII watchdog wrapper
│   └── main.cpp                 # Main application
├── platformio.ini               # Build configuration
├── README.md                    # This file
├── LOGGING_CONFIGURATION.md     # Detailed logging guide
└── MB8ART_USAGE_EXAMPLE.md      # Code examples
```

## 📡 System Architecture

### Task Overview

| Task | Priority | Interval | Purpose |
|------|----------|----------|---------|
| Temperature | 5 (High) | 2-10s | Read sensor data |
| Data Processing | 4 | 5-30s | Format/export data |
| Alarm | 3 | 10-60s | Monitor thresholds |
| Monitoring | 2 | 30-300s | System health |
| OTA | 1 (Low) | 2-5s | Check for updates |

### Event Flow
1. **TemperatureTask** reads all channels periodically
2. Sets `DATA_READY_BIT` in event group when complete
3. **DataProcessingTask** formats and logs data
4. **AlarmTask** checks for threshold violations
5. **MonitoringTask** reports system health

## 📈 Example Output

```
========================================
MB8ART Temperature Monitoring System
========================================

Logging: DEBUG SELECTIVE MODE
[I][MAIN] Initializing hardware...
[I][MAIN] Serial1 initialized: 9600 baud, RX=17, TX=18
[I][MAIN] Initializing network...
[I][NET] WiFi connected! IP: 192.168.1.100
[I][OTA] OTA service started on port 3232
[I][MAIN] Initializing Modbus...
[I][MODBUS] Modbus master started
[I][MAIN] MB8ART registered at address 0x01
[I][MAIN] MB8ART initialized successfully in 234 ms

Channel Diagnostics:
  Channel 1: THERMOCOUPLE TYPE_K
  Channel 2: THERMOCOUPLE TYPE_K
  Channel 3: PT_INPUT PT100
  Channel 4: VOLTAGE ±50mV
  Channel 5: DEACTIVATED
  Channel 6: DEACTIVATED
  Channel 7: DEACTIVATED
  Channel 8: DEACTIVATED

[I][MAIN] Starting FreeRTOS tasks...
[I][TEMP] Temperature acquisition task started
[I][MON] System monitoring task started

[I][MON] === System Monitor Report #1 ===
[I][MON] System Health:
[I][MON]   Heap: 45232/328736 bytes (13.8% used)
[I][MON]   Min Free Heap: 45232 bytes
[I][MON]   Uptime: 12 seconds
[I][MON] Network Status:
[I][MON]   WiFi: Connected
[I][MON]   SSID: MyNetwork
[I][MON]   IP: 192.168.1.100
[I][MON]   RSSI: -52 dBm
[I][MON] MB8ART Module Status:
[I][MON]   Module: Online
[I][MON]   Address: 0x01
[I][MON]   Total Requests: 6
[I][MON]   Successful: 6 (100.0%)
[I][MON]   Connection State: Connected
[I][MON]   Active Channels: 4/8
[I][MON] Temperature Statistics:
[I][MON]   Min: 22.3°C, Max: 24.8°C, Avg: 23.5°C
[I][MON]   Valid Channels: 4/8
[I][MON]   Last Read: 2 seconds ago
[I][MON] === End of Report ===
```

## 🔧 Configuration Guide

### Temperature Thresholds
Edit `src/config/ProjectConfig.h`:
```cpp
#define TEMP_ALARM_HIGH_THRESHOLD 80.0f   // °C
#define TEMP_ALARM_LOW_THRESHOLD -10.0f   // °C
#define TEMP_ALARM_HYSTERESIS 2.0f        // °C
```

### Task Intervals
Adjust based on your logging mode in `ProjectConfig.h`:
```cpp
// Release mode - conservative intervals
#define TEMPERATURE_TASK_INTERVAL_MS 10000    // 10 seconds
#define MONITORING_TASK_INTERVAL_MS 300000    // 5 minutes

// Debug mode - faster updates
#define TEMPERATURE_TASK_INTERVAL_MS 2000     // 2 seconds
#define MONITORING_TASK_INTERVAL_MS 30000     // 30 seconds
```

### Network Configuration
For WiFi (default):
```bash
export WIFI_SSID="your-network"
export WIFI_PASSWORD="your-password"
```

For Ethernet (requires hardware):
```cpp
// In ProjectConfig.h
#define USE_ETHERNET  // Instead of USE_WIFI
```

## 🐛 Troubleshooting

### Device Not Responding
```
[E][TEMP] MB8ART device is offline!
```
**Solutions:**
- Verify RS485 wiring (A+/B- not swapped)
- Check 120Ω termination resistors
- Confirm device address (default 0x01)
- Test with different baud rate
- Use oscilloscope to verify RS485 signals

### Temperature Reading Errors
```
[W][TEMP] Temperature read failed (failure #3)
```
**Solutions:**
- Check sensor connections
- Verify channel configuration matches sensor type
- Increase `MB8ART_RESPONSE_TIMEOUT_MS`
- Enable `mb8art_debug_full` for detailed logs

### WiFi Connection Issues
```
[W][NET] WiFi: Disconnected
```
**Solutions:**
- Verify SSID and password
- Check router settings (2.4GHz enabled)
- Increase `WIFI_CONNECTION_TIMEOUT_MS`
- Try static IP configuration

### Memory Issues
```
[W][MON] WARNING: Low memory! Free heap below 10000 bytes
```
**Solutions:**
- Switch to release build
- Reduce task stack sizes
- Disable unused features
- Check for memory leaks

## 📚 Advanced Topics

### Custom Channel Configuration
```cpp
// Configure all channels as K-type thermocouples
temperatureModule->configureAllChannels(
    mb8art::ChannelMode::THERMOCOUPLE,
    static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_K)
);

// Configure individual channel as PT100
temperatureModule->configureChannelMode(2, 
    static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT) | 
    (static_cast<uint16_t>(mb8art::PTType::PT100) << 8)
);
```

### Data Export Implementation
Extend `DataProcessingTask` to implement:
- CSV logging to SD card
- JSON formatting for web API
- MQTT publishing
- InfluxDB integration

### Custom Alarm Actions
Extend `AlarmTask` to add:
- Email notifications
- SMS alerts via Twilio
- Relay control for cooling/heating
- Data logging for alarm events

## 🔗 Resources

- [MB8ART Library Documentation](../../README.md)
- [ModbusDevice Framework](https://github.com/your-org/ModbusDevice)
- [esp32ModbusRTU Library](https://github.com/bertmelis/esp32ModbusRTU)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

## 📄 License

This example is provided as-is for demonstration purposes. Adapt it freely for your projects.