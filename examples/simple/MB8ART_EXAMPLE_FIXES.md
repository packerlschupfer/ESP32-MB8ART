# MB8ART Example Fixes

## Changes Made

### 1. Fixed platformio.ini Library Dependencies
- **Removed** invalid `ConsoleBackend` dependency (ConsoleBackend is part of Logger library, not separate)
- **Added** missing dependencies:
  - `IDeviceInstance` library 
  - `Watchdog` library
- **Updated** all `symlink://` URLs to `git+file://` for better git submodule support
- **Reordered** libraries to ensure proper dependency resolution

### 2. Fixed Compilation Warnings
- **Fixed** USE_WIFI redefinition warning in ProjectConfig.h by adding proper conditional check
- **Note**: CONFIG_ESP_TASK_WDT_TIMEOUT_S warning is expected (platformio.ini override of SDK default)

### 3. MB8ART Library Fixes (v1.3.4)
- **Fixed** temperature scaling bug - now correctly shows 24.4°C instead of 244°C
- **Fixed** address auto-correction issue - device maintains configured address
- **Fixed** initialization error spam - errors only shown if init fails
- **Fixed** channel configuration detection - properly tracks all 8 channels

### 4. Build Instructions
```bash
cd ~/.platformio/lib/ESP32-MB8ART/example/ESPlan-blueprint-libs-freertos-taskmanager-MB8ART-workspace
rm -rf .pio
pio run -e mb8art_debug_full
```

## API Status
- MB8ART library already uses the new `DeviceResult<std::vector<float>>` API ✓
- Example code already updated for new API ✓
- No changes needed for IDeviceInstance v4.0.0 compatibility ✓

## Testing the Example
1. Connect MB8ART module via RS485 (pins defined in ProjectConfig.h)
2. Set correct device address in ProjectConfig.h (default: 0x01)
3. Configure WiFi credentials if using OTA updates
4. Build and upload using instructions above
5. Monitor serial output to verify temperature readings

## Expected Output
```
[MB8ART][I] Device config register shows address 0 (stored value)
[MB8ART][I] Device is operating correctly at address 1
[MB8ART][I] All channels configured during init (0xFF)
[MB8ART][I] Processing PT data: Raw=0x00F4 (244), Type=PT1000, Temp=24.4°C
[MAIN][I] Temperature: 24.4°C
```