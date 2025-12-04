# MB8ART Hardware Register Documentation

This document describes the Modbus registers of the MB8ART 8-channel PT100/PT1000 temperature acquisition module, based on empirical testing and official documentation.

## Device Information

- **Model**: MB8ART (Equipment type 57)
- **Tested Firmware**: HW v34 / SW v34 (0x2222)
- **Communication**: RS485 Modbus RTU
- **Default Settings**: 9600 baud, 8N1, Address 1

## Register Map Overview

The MB8ART uses the following Modbus register types:
- **Holding Registers** (Function Code 03/06): Configuration and settings
- **Input Registers** (Function Code 04): Temperature readings and diagnostics
- **Discrete Inputs** (Function Code 02): Connection status per channel

**Note**: Modbus register addresses are 0-based. Tools like `mbpoll` use 1-based addressing, so add 1 to the register address when using mbpoll (e.g., register 64 = `mbpoll -r 65`).

## Holding Registers - System Configuration

| PLC Address | Register | Description | Range | Default | R/W |
|-------------|----------|-------------|-------|---------|-----|
| 40065 | 64 | Equipment type | - | 57 (MB8ART) | R |
| 40066 | 65 | Equipment status | Bit4: Reset button, Bit0: Reset request | 0x0000 | R |
| 40067 | 66 | Module voltage | 0.0-29.9V (×0.1V) | - | R |
| 40068 | 67 | Module temperature | -20.0 to 100.0°C (×0.1°C) | - | R |
| 40069 | 68 | Product version | High byte: HW, Low byte: SW | 0x2222 | R |
| 40070 | 69 | Production info | High byte: Year, Low byte: Batch | - | R |
| 40071 | 70 | Slave address | 1-247 | 1 | RW |
| 40072 | 71 | Baud rate | See table below | 3 (9600) | RW |
| 40073 | 72 | Parity | 0=None, 1=Even, 2=Odd | 0 | RW |
| 40074-40075 | 73-74 | Reserved | - | 0 | - |
| 40076 | 75 | Global sensor type | 0x00=PT100, 0x01=PT1000 | - | RW |
| 40077 | 76 | RTD Measuring range | 0x00=LOW_RES, 0x01=HIGH_RES | 0 | RW |

### Baud Rate Values (Register 71)

| Value | Baud Rate |
|-------|-----------|
| 0 | 1200 |
| 1 | 2400 |
| 2 | 4800 |
| 3 | 9600 (default) |
| 4 | 19200 |
| 5 | 38400 |
| 6 | 57600 |
| 7 | 115200 |

### Resolution Mode (Register 76)

| Value | Mode | Temperature Range | Resolution | Data Scaling |
|-------|------|-------------------|------------|--------------|
| 0x00 | LOW_RES | -200.0 to 850.0°C | 0.1°C | Divide by 10 |
| 0x01 | HIGH_RES | -200.00 to 200.00°C | 0.01°C | Divide by 100 |

**Note**: Both PT100 and PT1000 channels respond to the resolution setting. The global sensor type register (75) does not affect output scaling.

### Global Sensor Type (Register 75)

The purpose of this register is unclear. Testing shows it does NOT:
- Affect temperature output scaling (only register 76 does that)
- Auto-configure closed channels
- Change per-channel configurations

It may be reserved for future use or specific firmware versions.

## Holding Registers - Channel Configuration

| Register | Description | Format |
|----------|-------------|--------|
| 128-135 | Channel 0-7 configuration | 0xTTSS |

### Channel Configuration Format (0xTTSS)

- **TT (High byte)**: Input type
- **SS (Low byte)**: Subtype (depends on input type)

### Input Types (High Byte)

| Value | Input Type |
|-------|------------|
| 0x00 | Channel Closed |
| 0x01 | Thermocouple Input |
| 0x02 | PT/RTD Input |
| 0x03 | Voltage Input |
| 0x04 | Current Input |

### Subtypes (Low Byte)

#### Thermocouple (0x01xx)

| Value | Type | Temperature Range |
|-------|------|-------------------|
| 0x0100 | J-type TC | -200.0 to 1200.0°C |
| 0x0101 | K-type TC | -200.0 to 1370.0°C |
| 0x0102 | T-type TC | -200.0 to 400.0°C |
| 0x0103 | E-type TC | -200.0 to 950.0°C |
| 0x0104 | R-type TC | -20.0 to 1750.0°C |
| 0x0105 | S-type TC | -20.0 to 1750.0°C |
| 0x0106 | B-type TC | 600.0 to 1800.0°C |
| 0x0107 | N-type TC | -200.0 to 1300.0°C |

**Note**: Thermocouple data is always in tenths (÷10), not affected by resolution register.

#### PT/RTD (0x02xx)

| Value | Type |
|-------|------|
| 0x0200 | PT100 |
| 0x0201 | PT1000 |
| 0x0202 | CU50 |
| 0x0203 | CU100 |

#### Voltage (0x03xx)

| Value | Range |
|-------|-------|
| 0x0300 | ±15mV |
| 0x0301 | ±50mV |
| 0x0302 | ±100mV |
| 0x0303 | ±1V |

#### Current (0x04xx)

| Value | Range |
|-------|-------|
| 0x0400 | ±20mA |
| 0x0401 | 4-20mA |

## Input Registers - Temperature Data

| Register | Description | Format |
|----------|-------------|--------|
| 0-7 | Channel 0-7 temperature | Signed 16-bit integer |

### Temperature Data Interpretation

The raw register value must be scaled based on the resolution mode (register 76).

| Resolution Mode | Scaling | Example |
|-----------------|---------|---------|
| LOW_RES (0) | ÷10 | 234 = 23.4°C |
| HIGH_RES (1) | ÷100 | 2340 = 23.40°C |

### Special Values

| Value | Meaning |
|-------|---------|
| 30000 | Sensor disconnected (open circuit) |
| -30000 | Sensor error (wrong type or short circuit) |

## Input Registers - Diagnostics

| Register | Description | Format |
|----------|-------------|--------|
| 8 | Module temperature | Tenths of °C (÷10) |
| 9 | Reserved | Always 0 |
| 10 | Calibration value | Factory calibration (unknown purpose) |
| 11 | Reference resistor | Ohms (nominal 10000Ω) |
| 12-15 | Reserved | Always 0 |

### Reference Resistor (Register 11)

This is the factory-calibrated value of the precision reference resistor used for RTD measurement. Typical values are close to 10000Ω (10kΩ).

## Discrete Inputs - Connection Status (Function Code 02)

| Register | Description |
|----------|-------------|
| 0-7 | Channel 0-7 connection status |
| 8-15 | Not used (always 0) |

| Value | Meaning |
|-------|---------|
| 0 | Sensor disconnected |
| 1 | Sensor connected |

## Coils (Function Code 01)

| Register | Description |
|----------|-------------|
| 0-9 | Unknown (always 0) |
| 10-15 | Unknown (always 1) |
| 16+ | Not implemented (timeout) |

**Note**: The purpose of coils 10-15 is unknown. They do not correspond to channel connection status (use Discrete Inputs for that). They may be internal status flags.

## Firmware Notes

### Version Tested: HW v34 / SW v34

**Known Limitations**:
- **Module voltage (register 66)**: Not implemented in this firmware version - always returns 0
- **Software reset (register 65)**: Writing to register 65 has no effect - software reset not implemented
- **Global sensor type (register 75)**: Purpose unknown - does not affect output scaling or channel configuration

**Physical Reset Button**:
- Resets communication parameters only: Address→1, Baud→9600, Parity→None
- Does NOT reset channel configurations or resolution mode

### Product Version Decoding (Register 68)

Format: 0xHHSS where:
- HH = Hardware version
- SS = Software version

Example: 0x2222 = HW version 34, SW version 34 (or v3.4/v3.4)

### Production Info Decoding (Register 69)

Format: 0xYYBB where:
- YY = Year (add 2000)
- BB = Batch number

Example: 0x1301 = Year 2019 (0x13 = 19), Batch 01

## Communication Examples

### Using mbpoll

#### Changing Device Address

```bash
# Change address from 1 to 3 (write to register 70)
mbpoll -m rtu -a 1 -t 4 -r 71 -b 9600 -P none /dev/ttyUSB0 3

# Device now responds only to address 3
mbpoll -m rtu -a 3 -r 65 -t 4 -c 1 -b 9600 -P none /dev/ttyUSB0
```

**Note**: The address change takes effect immediately. Use the new address for all subsequent commands.

#### Reading Registers

```bash
# Read system registers (64-77)
mbpoll -m rtu -a 1 -r 65 -t 4 -c 13 -b 9600 -P none /dev/ttyUSB0

# Read channel configurations (128-135)
mbpoll -m rtu -a 1 -r 129 -t 4:hex -c 8 -b 9600 -P none /dev/ttyUSB0

# Read temperatures (input registers 0-7)
mbpoll -m rtu -a 1 -r 1 -t 3 -c 8 -b 9600 -P none /dev/ttyUSB0

# Read connection status (discrete inputs 0-7)
mbpoll -m rtu -a 1 -r 1 -t 1 -c 8 -b 9600 -P none /dev/ttyUSB0

# Set HIGH_RES mode (register 76 = 1)
mbpoll -m rtu -a 1 -t 4 -r 77 -b 9600 -P none /dev/ttyUSB0 1

# Configure channel 0 as PT1000 (register 128 = 0x0201)
mbpoll -m rtu -a 1 -t 4:hex -r 129 -b 9600 -P none /dev/ttyUSB0 0x0201

# Configure channel 0 as PT100 (register 128 = 0x0200)
mbpoll -m rtu -a 1 -t 4:hex -r 129 -b 9600 -P none /dev/ttyUSB0 0x0200

# Configure channel 0 as K-type thermocouple (register 128 = 0x0101)
mbpoll -m rtu -a 1 -t 4:hex -r 129 -b 9600 -P none /dev/ttyUSB0 0x0101
```

## Wiring

### PT100/PT1000 Three-Wire Connection (Recommended)
```
Sensor      Module
------      ------
Wire 1  →   RTDx+
Wire 2  →   RTDx-
Wire 3  →   GND (compensation)
```

### PT100/PT1000 Two-Wire Connection
```
Sensor      Module
------      ------
Wire 1  →   RTDx+
Wire 2  →   RTDx-
        →   Short RTDx- to GND
```

**Note**: Three-wire connection is recommended for better accuracy as it compensates for lead wire resistance.

## Power Supply

- **Input Voltage**: DC 8-30V
- **Power Consumption**:
  - 9mA @ 30V
  - 12mA @ 24V
  - 23mA @ 12V
  - 33mA @ 8V

## Physical Specifications

- **Dimensions**: 88 × 72 × 59 mm
- **Mounting**: 35mm DIN rail
- **Material**: ABS
- **Operating Temperature**: -30°C to +55°C
- **Operating Humidity**: 0-95% RH (non-condensing)
