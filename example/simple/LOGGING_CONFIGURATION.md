# Logging Configuration for MB8ART Example

This guide explains how to configure logging for MB8ART library which uses ModbusDevice and esp32ModbusRTU.

## Option 1: Use Standard ESP-IDF Logging (Default)

No configuration needed. Both libraries will use ESP-IDF logging by default.

```ini
# platformio.ini - Standard ESP-IDF logging
build_flags =
    # Legacy flags for esp32ModbusRTU (still supported)
    -D ESP32MODBUSRTU_DEBUG       ; Legacy flag for esp32ModbusRTU
    -D ESP32MODBUSMSG_DEBUG       ; Legacy flag for esp32ModbusRTU messages
    
    # New unified flags (recommended)
    -D MODBUS_DEBUG               ; Enable esp32ModbusRTU debug logs
    -D MODBUSDEVICE_DEBUG         ; Enable ModbusDevice debug logs
    -D MB8ART_DEBUG               ; Enable MB8ART debug logs
    
    -D CORE_DEBUG_LEVEL=5         ; ESP-IDF verbose logging
```

## Option 2: Use Custom Logger (Recommended)

Enable custom Logger for unified logging across all libraries using LogInterface:

```ini
# platformio.ini - Custom Logger for unified logging
build_flags =
    # Enable custom Logger globally (LogInterface handles all libraries)
    -D USE_CUSTOM_LOGGER

    # Enable debug logging
    -D MODBUS_DEBUG               ; esp32ModbusRTU debug logs
    -D MODBUSDEVICE_DEBUG         ; ModbusDevice debug logs
    -D MB8ART_DEBUG               ; MB8ART debug logs
    
    # Optional: MB8ART specific debug levels
    # -D MB8ART_DEBUG_INIT         ; Enable initialization debug logs only
    # -D MB8ART_DEBUG_FULL         ; Enable all MB8ART debug logs
```

Note: With LogInterface (Logger v2.0.0+), you only need to define `USE_CUSTOM_LOGGER` once globally. All libraries that use LogInterface will automatically use the custom Logger when available.

## Log Tags Used

- `"ModbusD"` - ModbusDevice library logs
- `"ModbusRTU"` - esp32ModbusRTU library logs (when using MODBUS_DEBUG)
- `"esp32ModbusRTU"` - esp32ModbusRTU library logs (when using ESP32MODBUSRTU_DEBUG)
- `"MB8ART"` - MB8ART device logs

## Benefits of Custom Logger

1. Unified formatting across all libraries
2. Single point of control for log filtering/routing
3. Consistent timestamps and metadata
4. Easy to redirect logs to SD card, network, etc.
5. Better performance with compile-time log level filtering

## Example Integration

```cpp
// In your main.cpp
#include "Logger.h"
#include "LogInterfaceImpl.h"  // Required once for LogInterface

void setup() {
    // Configure your custom logger once
    Logger::getInstance().setLevel(ESP_LOG_DEBUG);
    
    // Optional: Configure specific tags
    Logger::getInstance().setTagLevel("MB8ART", ESP_LOG_DEBUG);
    Logger::getInstance().setTagLevel("ModbusD", ESP_LOG_INFO);
    Logger::getInstance().setTagLevel("ModbusRTU", ESP_LOG_WARN);

    // All libraries using LogInterface will automatically
    // use this logger instance when USE_CUSTOM_LOGGER is defined
}
```

This gives you complete control over logging across the entire Modbus stack.

## Current Configuration

The example provides multiple build environments:

### Default Environments (with Logger)
- `mb8art_release`, `mb8art_debug_selective`, `mb8art_debug_full` - Include Logger library by default
- Use custom Logger for consistent formatting across all components

### Custom Logger Environment
- `mb8art_custom_logger` - Enables custom logger globally:
  - `USE_CUSTOM_LOGGER` - Single flag enables custom Logger for all LogInterface-compatible libraries
  - All libraries using LogInterface will automatically use the custom Logger

### No Logger Environment
- `mb8art_no_logger` - Demonstrates usage without Logger dependency
- Uses only ESP-IDF logging (ESP_LOGE, ESP_LOGW, ESP_LOGI, ESP_LOGD)
- Excludes Logger and ConsoleBackend libraries from the build
- All libraries fall back to their default ESP-IDF logging

## Migration Notes

The esp32ModbusRTU library supports both legacy flags (`ESP32MODBUSRTU_DEBUG`) and new unified flags (`MODBUS_DEBUG`). For new projects, use the unified flags. The legacy flags are maintained for backward compatibility.

## Testing Different Configurations

### Testing with Custom Logger
```bash
# Clean build and upload with custom logger
pio run -e mb8art_custom_logger -t clean
pio run -e mb8art_custom_logger -t upload
pio device monitor
```

Expected output:
```
Logging: DEBUG SELECTIVE MODE (Custom Logger)
```

### Testing without Logger
```bash
# Clean build and upload without logger dependency
pio run -e mb8art_no_logger -t clean
pio run -e mb8art_no_logger -t upload
pio device monitor
```

Expected output:
```
Logging: RELEASE MODE (ESP-IDF)
```

### Comparing Log Output

With Custom Logger:
```
[I][MAIN] Initializing hardware...
[I][MB8ART] Device initialized at address 0x01
```

Without Logger (ESP-IDF):
```
I (1234) MAIN: Initializing hardware...
I (2345) MB8ART: Device initialized at address 0x01
```

The ESP-IDF format includes timestamps in milliseconds, while Custom Logger provides more control over formatting.