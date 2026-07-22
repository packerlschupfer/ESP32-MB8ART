# MB8ART Examples

Three example projects demonstrating MB8ART library usage.

## Examples

### [MB8ART-BasicExample/](MB8ART-BasicExample/) - Minimal Example

The simplest possible example with minimal dependencies:
- Single main.cpp (~200 lines)
- Demonstrates core MB8ART API
- Periodic temperature reading from all 8 channels
- Sensor connection status checking
- Uses symlink dependencies for local development
- Perfect for getting started quickly

### [simple/](simple/) - Quick Start Example

Minimal example to get MB8ART working quickly:
- Single main.cpp with inline initialization
- All configuration in one ProjectConfig.h
- Multiple task examples (temperature, monitoring, alarms)
- Good for learning and prototyping

### [full/](full/) - Production Architecture

Production-ready example with modular architecture:
- SystemInitializer pattern with proper error handling
- Result<T> error propagation
- Clean separation of concerns (init/, tasks/, config/)
- FreeRTOS task management with watchdog integration
- Ethernet networking with OTA updates
- Three-tier logging (Release/Debug Selective/Debug Full)
- Graceful degradation (continues without network)

## Which to Use?

| Use Case | Recommended |
|----------|-------------|
| Getting started | `MB8ART-BasicExample/` |
| Learning MB8ART API | `MB8ART-BasicExample/` or `simple/` |
| Quick prototype | `simple/` |
| Production system | `full/` |
| Multiple Modbus devices | `full/` |
| Need OTA updates | `full/` |

## Hardware Requirements

All examples require:
- ESP32 development board
- MB8ART 8-channel temperature sensor
- RS485 transceiver (MAX485 or similar)

The `full/` example also uses:
- LAN8720 Ethernet PHY

## Quick Start

```bash
# Basic example (recommended for beginners)
cd MB8ART-BasicExample
pio run
pio device monitor

# Simple example
cd simple
pio run -e esp32dev_usb_debug_selective
pio device monitor

# Full example
cd full
pio run -e esp32dev_debug_selective
pio device monitor
```
