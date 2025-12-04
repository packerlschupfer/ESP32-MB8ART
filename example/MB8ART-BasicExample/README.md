# MB8ART Basic Example

Simple demonstration of the MB8ART 8-channel temperature sensor library for ESP32.

## Features Demonstrated

- Modbus RTU initialization (8N1 - factory default parity)
- MB8ART device setup and initialization
- Reading temperature values from all 8 channels
- Sensor connection status checking
- Module internal temperature reading
- Periodic temperature monitoring

## Hardware Requirements

- ESP32 development board
- MB8ART 8-channel temperature sensor module
- RS485 transceiver (e.g., MAX485, SP3485)

## Wiring

| ESP32 | MAX485 | MB8ART Module |
|-------|--------|---------------|
| GPIO17 (TX) | DI | - |
| GPIO16 (RX) | RO | - |
| GPIO4 | DE+RE | - |
| - | A | A+ |
| - | B | B- |
| GND | GND | GND |

## MB8ART Module Setup

1. **Slave Address**: Default is 0x03
   - Can be configured via Modbus holding registers

2. **Baud Rate**: 9600 baud (default)
   - Can be configured via Modbus holding registers

3. **Sensor Inputs**: PT1000 temperature sensors
   - Connect sensors to channels 1-8 as needed

## Building

```bash
cd example/MB8ART-BasicExample
pio run
```

## Uploading

```bash
pio run -t upload
```

## Monitor Output

```bash
pio device monitor
```

Expected output:
```
========================================
MB8ART Basic Example
8-Channel Temperature Sensor Demo
========================================

Initializing Modbus RTU...
Modbus RTU initialized (9600 baud, 8N1)
Creating MB8ART device at address 0x03...
Initializing MB8ART...
MB8ART initialized successfully!

Checking sensor connections...
Sensor Status: S1:OK S2:OK S3:NC S4:NC S5:NC S6:NC S7:NC S8:NC

--- Reading #1 ---
Temperatures: T1:23.5°C T2:24.1°C T3:--- T4:--- T5:--- T6:--- T7:--- T8:---

--- Reading #2 ---
Temperatures: T1:23.6°C T2:24.2°C T3:--- T4:--- T5:--- T6:--- T7:--- T8:---
...
```

## Customization

Edit `src/main.cpp` to change:
- GPIO pins for Modbus (lines 28-31)
- MB8ART slave address (line 34)
- Reading interval (line 37)

## Troubleshooting

1. **"MB8ART initialization failed"**
   - Check RS485 wiring (A/B polarity)
   - Verify slave address matches module configuration
   - Ensure power supply is adequate

2. **No temperature readings**
   - Check sensor connections (PT1000 sensors)
   - Verify sensor type configuration matches connected sensors
   - Check for open circuit or short circuit on sensor inputs

3. **Intermittent communication**
   - Add 120 ohm termination resistor on long RS485 runs
   - Check for loose connections
   - Ensure proper grounding

## Temperature Data

- Temperatures are stored internally as `int16_t` raw values
- LOW_RES mode: value / 10 = temperature in °C (e.g., 235 = 23.5°C)
- HIGH_RES mode: value / 100 = temperature in °C (e.g., 2350 = 23.50°C)
- The example uses `getDataScaleDivider()` to handle scaling automatically
