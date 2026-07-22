# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-12-04

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

Platform: ESP32 (Arduino/ESP-IDF)
Hardware: MB8ART 8-channel temperature module (Modbus RTU)
License: MIT
Dependencies: ESP32-ModbusDevice, ESP32-IDeviceInstance, MutexGuard

### Notes
- Production-tested reading 8 thermocouples every 2.5s with 99.9%+ reliability
- Previous internal versions (v1.x-v3.x) not publicly released
- Reset to v0.1.0 for clean public release start
