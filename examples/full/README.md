# MB8ART Full Example - Modular Architecture

Production-ready example demonstrating best practices for ESP32 + MB8ART integration.

## Features

- **Modular Initialization** - SystemInitializer pattern with proper error handling
- **FreeRTOS Tasks** - Watchdog-monitored temperature acquisition and system monitoring
- **Ethernet + OTA** - LAN8720 network with over-the-air firmware updates
- **Three-Tier Logging** - Release/Debug Selective/Debug Full modes
- **Graceful Degradation** - Continues operation even if network fails

## Hardware Requirements

- ESP32 development board
- LAN8720 Ethernet PHY module
- MB8ART 8-channel PT1000 temperature sensor (RS485)
- RS485 transceiver (MAX485 or similar)

## Pin Configuration

| Function | GPIO | Notes |
|----------|------|-------|
| ETH_MDC | 23 | Ethernet MDC |
| ETH_MDIO | 18 | Ethernet MDIO |
| ETH_CLK | 17 | 50MHz clock output |
| RS485_RX | 36 | Modbus receive |
| RS485_TX | 4 | Modbus transmit |
| STATUS_LED | 2 | Onboard LED |

## Build Environments

```bash
# Debug selective (default) - strategic debug output
pio run -e esp32dev_debug_selective

# Debug full - maximum verbosity
pio run -e esp32dev_debug_full

# Release - production optimized
pio run -e esp32dev_release
```

## Architecture

```
src/
├── main.cpp              # Entry point, setup/loop
├── init/
│   └── SystemInitializer.cpp  # Modular initialization
└── tasks/
    ├── TemperatureTask.cpp    # MB8ART temperature reading
    └── MonitoringTask.cpp     # System health monitoring

include/
├── config/
│   └── ProjectConfig.h   # All configuration in one place
└── init/
    └── SystemInitializer.h
```

## Initialization Sequence

1. **Logging** - Serial and custom Logger setup
2. **Hardware** - GPIO, RS485 serial port
3. **Network** - Ethernet connection, OTA service
4. **Modbus** - MB8ART device initialization
5. **Tasks** - FreeRTOS task creation with watchdog

## Usage

1. Copy `platformio.ini` and adjust pin definitions for your hardware
2. Set `MB8ART_ADDRESS` to match your device
3. Build and upload
4. Monitor serial output at 921600 baud

## OTA Updates

Once running, update firmware via:

```bash
pio run -t upload --upload-port esp32-mb8art-full.local
# or by IP
pio run -t upload --upload-port 192.168.x.x
```

Default OTA password: `mb8art-update`

## See Also

- `../simple/` - Minimal example for quick start
- ESP32-MB8ART library documentation
