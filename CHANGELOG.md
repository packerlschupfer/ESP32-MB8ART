# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-10-19

### Added
- Initial public release
- 8-channel temperature acquisition via Modbus RTU
- Support for PT100, PT1000, and thermocouple sensors
- Fixed-point arithmetic (Temperature_t, tenths of °C)
- Non-blocking async reads via FreeRTOS tasks
- IDeviceInstance interface implementation
- QueuedModbusDevice base class for async operations
- Built-in error detection (0xFFFF, 0x0000, 0x7530)
- Cache validity and staleness detection
- Configurable retry logic with timeout handling
- Batch configuration reading to minimize initialization time
- Passive responsiveness monitoring
- Connection status caching (5-second cache)
- Pre-computed active channel masks for performance
- Memory-optimized with bit fields
- Thread-safe operations with mutex protection
- Event group integration for FreeRTOS synchronization
- High-resolution and standard measurement modes
- Per-channel temperature calibration offsets: `setChannelOffset()`, `setChannelOffsets()`,
  `getChannelOffset()`. Offsets are in tenths of a degree (±500) and are applied to the value
  written through the bound temperature pointer, only for temperature channels (PT,
  thermocouple), checked per reading: current/voltage channels are never offset, and offsets set
  before the channel config has been read still take effect. `getSensorReadings()`, `getData()`
  and `getDataRaw()` stay uncorrected.
- Native unit tests for the offset rules (`test/test/test_channel_offsets`).

### Removed
- `applyTemperatureCorrection()`, a private placeholder with a fixed 0 offset that was never called.

### Fixed
- HIGH_RES upper range limit (85000) overflowed `int16_t` and wrapped to 19464, so readings
  from 194.65 °C to 200.00 °C were rejected; now 20000 (200.00 °C, the specified HIGH_RES range).
- Resolution is applied per channel, as `getData()` already did: thermocouples in HIGH_RES were
  read as hundredths (bound value 10x too low), thermocouple readings above 850.0 °C were
  rejected (now valid to 1800.0 °C), and current channels in LOW_RES were written to the bound
  pointer unconverted. PT/RTD channels are unchanged.
- Stale comments claiming current-mode data is returned raw; it is scaled to hundredths of mA.
- PlatformIO's dependency scan followed `TemperatureControlModule.h`'s `#ifdef MB8ART_ENABLE_MQTT`
  include of `IMqttMessageHandler.h` even when MQTT was off, pulling in ESP32-MQTTManager wherever it
  was reachable (e.g. global libraries) and failing the build. `library.json` now sets
  `libLDFMode: deep+`, which evaluates the guard: MQTTManager is used only when the flag is defined.

Platform: ESP32 (Arduino/ESP-IDF)
Hardware: MB8ART 8-channel temperature module (Modbus RTU)
License: GPL-3
Dependencies: ESP32-ModbusDevice, ESP32-IDeviceInstance, MutexGuard

### Notes
- Production-tested reading 8 thermocouples every 2.5s with 99.9%+ reliability
- Previous internal versions (v1.x-v3.x) not publicly released
- Reset to v0.1.0 for clean public release start
