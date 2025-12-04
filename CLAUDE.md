# MB8ART Library - CLAUDE.md

## Overview
MB8ART is a FreeRTOS-based library for interfacing with MB8ART 8-channel temperature sensor modules via Modbus RTU. The library has been migrated to use the new ModbusDevice v2.0.0 architecture and recently underwent a major refactoring to split the monolithic source file into multiple logical components.

## Key Features
- Support for 8 temperature channels with PT1000 sensors
- Batch configuration reading to minimize initialization time
- Passive responsiveness monitoring (inspired by RYN4 library)
- Thread-safe operation with proper mutex usage
- Memory-optimized with bit fields
- Pre-computed channel masks for performance
- Connection status caching to reduce bus traffic

## Recent Changes

### Code Refactoring (2025-06-29)
The library underwent a major refactoring to improve maintainability:

#### File Structure
- **MB8ART.cpp** (1733 lines) - Core initialization and private helper methods
- **MB8ARTDevice.cpp** (490 lines) - IDeviceInstance interface implementation
- **MB8ARTModbus.cpp** (575 lines) - Modbus communication and response handling
- **MB8ARTState.cpp** (192 lines) - State query and management methods
- **MB8ARTConfig.cpp** (259 lines) - Configuration and settings management
- **MB8ARTSensor.cpp** (426 lines) - Sensor operations and data processing
- **MB8ARTEvents.cpp** (173 lines) - Event management and bit operations

Original monolithic file was 2728 lines - now split into logical components totaling ~3850 lines (including proper headers and organization).

### Temperature Display Fix (2025-06-29)
- Fixed HIGH_RES mode to properly display 2 decimal places (26.89°C instead of 26.9°C)
- Temperature scaling now correctly divides by 100 for HIGH_RES mode
- Added proper format string selection based on measurement range

## Recent Optimizations (2025-08-01)

### 7. Async Queue Size Optimization
- Reduced async queue size from 15 to 8 slots (saves ~200 bytes RAM)
- Analysis showed MB8ART typically uses 2-3 concurrent requests
- Queue size is now configurable via `MB8ART_ASYNC_QUEUE_SIZE`
- Projects can override with `PROJECT_MB8ART_ASYNC_QUEUE_SIZE` if needed

## Recent Optimizations (2025-06-29)

### 1. Global Timestamp Tracking for hasRecentSensorData()
- Added `lastAnyChannelUpdate` member variable
- Check global timestamp first before iterating through channels
- Significantly reduces CPU usage for frequent data freshness checks

### 2. Thread-Safe Logging
- Fixed static buffer corruption in temperature data processing
- Changed from `static char statusBuffer[256]` to local buffer
- Ensures thread safety in multi-task environments

### 3. Connection Status Caching
- Added 5-second cache for connection status (CONNECTION_STATUS_CACHE_MS)
- Prevents excessive polling of connection status registers
- Updates cache on both explicit requests and unsolicited responses

### 4. Pre-computed Active Channel Mask
- Added `activeChannelMask` and `activeChannelCount` members
- `updateActiveChannelMask()` called after channel configuration changes
- Eliminates repeated channel iteration in `waitForData()` and `clearDataEventBits()`

### 5. Batch Configuration Writing
- Implemented `configureAllChannels()` and `configureChannelRange()`
- Allows configuring multiple channels in a single Modbus transaction
- Added `configureMeasurementRange()` for resolution control

### 6. Memory Optimization with Bit Fields
- Converted boolean flags in `SensorReading` struct to bit fields (4 bytes to 1 byte)
  - Added constructor for proper initialization
  - Changed from designated initializers to constructor calls
- Optimized MB8ART class with `statusFlags` bit field structure
  - `initialized`, `moduleOffline`, `optionalSettingsPending` now use single byte
- Changed `sensorConnected[8]` array to single `uint8_t` with bit operations
  - Helper methods: `isSensorConnected()` and `setSensorConnected()`
  - Each sensor uses 1 bit instead of 1 byte
- Memory savings: ~12 bytes per MB8ART instance, ~4 bytes per sensor reading

## Build and Test Commands

```bash
# Clean build
rm -rf .pio

# Build the project
pio run

# Run with specific debug output
pio device monitor -f esp32_exception_decoder

# Lint and typecheck (if available)
# Add specific commands here when determined
```

## Critical Register Addresses
- Temperature data: 0-7 (Input registers)
- Connection status: 0-7 (Discrete inputs) 
- Module temperature: 67
- Measurement range: 76
- Channel configs: 128-135

## Modbus Timing
- Min request interval: 25ms (MB8ART_MIN_REQUEST_INTERVAL_MS)
- Request timeout: 500ms (MB8ART_REQUEST_TIMEOUT_MS)
- Inter-request delay: 5ms (MB8ART_INTER_REQUEST_DELAY_MS)

## Key Architectural Decisions

### Batch Reading Strategy
1. Channel configurations (128-135) are read first as they're critical
2. Module settings (67-76) are read second as they're optional "eye candy"
3. Total initialization time reduced from 16+ requests to 2 batch reads

### Passive Responsiveness Monitoring
Following RYN4 suggestion:
- Track `lastResponseTime` on ANY Modbus response
- Check this timestamp first in `isModuleResponsive()`
- Avoids unnecessary polling during normal operation

### Thread Safety
- All shared resources protected by mutexes
- Event groups used for inter-task communication
- Atomic operations for counters accessed from multiple tasks

## Integration with TaskManager
The library integrates with TaskManager for watchdog monitoring:
- Tasks should call `feedWatchdog()` regularly
- Use `registerCurrentTaskWithWatchdog()` for critical tasks
- Unregister before task deletion with `unregisterCurrentTaskFromWatchdog()`

## Future Improvements
- Add support for thermocouple and voltage/current input modes
- Implement alarm/threshold monitoring
- Add data logging capabilities
- Support for multiple MB8ART devices on same bus