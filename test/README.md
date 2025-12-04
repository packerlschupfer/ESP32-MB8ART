# MB8ART Unit Tests

This directory contains comprehensive unit tests for the MB8ART library using the Unity test framework.

## Overview

The test suite includes:
- **MockMB8ART**: A hardware-independent mock implementation for testing
- **Core functionality tests**: Basic operations, initialization, data access
- **Configuration tests**: Channel configuration, measurement ranges, RS485 settings
- **Error handling tests**: Communication errors, edge cases, recovery scenarios

## Test Structure

### MockMB8ART.h
A mock implementation that simulates MB8ART hardware behavior without requiring actual devices:
- Configurable temperature values
- Simulated Modbus responses
- Error injection capabilities
- Request tracking and counters

### Test Files
1. **test_mb8art.cpp** - Core functionality tests
   - Initialization
   - IModbusInput interface
   - Temperature requests
   - Basic operations

2. **test_mb8art_configuration.cpp** - Configuration tests
   - Channel configuration
   - Measurement range settings
   - RS485 parameters
   - Factory reset

3. **test_mb8art_error_handling.cpp** - Error and edge case tests
   - Communication errors
   - Data validation
   - Concurrent access
   - Recovery scenarios

## Running Tests

### Prerequisites
- PlatformIO CLI installed
- Unity test framework (automatically installed by PlatformIO)

### Running All Tests
```bash
./run_tests.sh
```

### Running Specific Test File
```bash
./run_tests.sh test_mb8art_configuration
```

### Running on Hardware (ESP32)
```bash
./run_tests.sh --hardware
```

### Direct PlatformIO Commands
```bash
# Run all native tests
platformio test -e native

# Run specific test
platformio test -e native -f test_mb8art

# Run with verbose output
platformio test -e native --verbose

# Run on ESP32 hardware
platformio test -e esp32
```

## Writing New Tests

### Test Structure
```cpp
#include <unity.h>
#include "MockMB8ART.h"

std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    mb8art.reset();
}

void test_my_feature() {
    // Arrange
    mb8art->initialize();
    mb8art->setMockTemperature(0, 25.5f);
    
    // Act
    auto result = mb8art->getValue(0);
    
    // Assert
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.5f, result.value());
}
```

### Mock Configuration
The MockMB8ART class provides various methods to configure test scenarios:

```cpp
// Set temperature values
mb8art->setMockTemperature(channel, temperature, connected);

// Configure measurement range
mb8art->setMockMeasurementRange(mb8art::MeasurementRange::HIGH_RES);

// Configure channel types
mb8art->setMockChannelConfig(channel, mode, subType);

// Simulate errors
mb8art->setDeviceOffline(true);
mb8art->simulateError(ModbusError::TIMEOUT);

// Track requests
uint32_t count = mb8art->getTemperatureRequestCount();
```

## Test Coverage

Current test coverage includes:
- ✅ Initialization sequences
- ✅ Temperature data reading
- ✅ Channel configuration
- ✅ Error handling
- ✅ Edge cases and boundaries
- ✅ IModbusInput interface compliance
- ✅ Measurement range handling
- ✅ RS485 configuration

## Continuous Integration

These tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run MB8ART Tests
  run: |
    cd test
    platformio test -e native --json-output > test_results.json
```

## Debugging Tests

To debug failing tests:
1. Run with verbose output: `platformio test -e native --verbose`
2. Add debug prints in test code
3. Use Unity's detailed assertions for better error messages
4. Check MockMB8ART state with getter methods

## Future Improvements

- [ ] Add performance benchmarking tests
- [ ] Test memory usage and leaks
- [ ] Add stress tests for long-running scenarios
- [ ] Mock more complex Modbus scenarios
- [ ] Add integration tests with actual ModbusDevice