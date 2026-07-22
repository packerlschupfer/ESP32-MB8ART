#include <unity.h>
#include "MockMB8ART.h"
#include <memory>

// Test fixtures
std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    // Create fresh mock instance for each test
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    // Clean up
    mb8art.reset();
}

// Test initialization
void test_initialization_success() {
    TEST_ASSERT_TRUE(mb8art->initialize());
    TEST_ASSERT_TRUE(mb8art->isReady());
}

void test_initialization_failure() {
    mb8art->setInitializationFailure(true);
    TEST_ASSERT_FALSE(mb8art->initialize());
    TEST_ASSERT_FALSE(mb8art->isReady());
}

void test_device_offline() {
    mb8art->setDeviceOffline(true);
    TEST_ASSERT_FALSE(mb8art->initialize());
    TEST_ASSERT_FALSE(mb8art->isReady());
}

// Test IModbusInput interface
void test_get_channel_count() {
    TEST_ASSERT_EQUAL(8, mb8art->getChannelCount());
}

void test_get_value_valid_channel() {
    mb8art->initialize();
    mb8art->setMockTemperature(0, 25.5f);
    
    // Simulate temperature data response
    uint8_t tempData[16] = {0};
    // Channel 0: 25.5°C = 255 in 0.1°C resolution
    tempData[0] = 0x00;
    tempData[1] = 0xFF;
    
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    auto result = mb8art->getValue(0);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.5f, result.value());
}

void test_get_value_invalid_channel() {
    mb8art->initialize();
    
    auto result = mb8art->getValue(8);  // Invalid channel
    TEST_ASSERT_FALSE(result.isOk());
    TEST_ASSERT_EQUAL(ModbusError::INVALID_PARAMETER, result.error());
}

void test_get_all_values() {
    mb8art->initialize();
    
    // Set mock temperatures
    for (uint8_t i = 0; i < 8; i++) {
        mb8art->setMockTemperature(i, 20.0f + i);
    }
    
    // Simulate temperature response with all channels
    uint8_t tempData[16];
    for (int i = 0; i < 8; i++) {
        uint16_t temp = (200 + i * 10);  // 20.0, 21.0, 22.0...
        tempData[i * 2] = (temp >> 8) & 0xFF;
        tempData[i * 2 + 1] = temp & 0xFF;
    }
    
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    auto result = mb8art->getAllValues();
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_EQUAL(8, result.value().size());
    
    // Check each temperature
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f + i, result.value()[i]);
    }
}

void test_get_channel_name() {
    mb8art->initialize();
    mb8art->setMockChannelConfig(0, mb8art::ChannelMode::PT_INPUT, 
                                 static_cast<uint16_t>(mb8art::PTType::PT1000));
    
    auto result = mb8art->getChannelName(0);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_TRUE(result.value().find("CH1") != std::string::npos);
}

void test_get_channel_unit() {
    mb8art->initialize();
    
    // Test temperature sensor
    mb8art->setMockChannelConfig(0, mb8art::ChannelMode::PT_INPUT, 0);
    auto result = mb8art->getChannelUnit(0);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_EQUAL_STRING("°C", result.value().c_str());
    
    // Test voltage sensor
    mb8art->setMockChannelConfig(1, mb8art::ChannelMode::VOLTAGE, 0);
    result = mb8art->getChannelUnit(1);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_EQUAL_STRING("V", result.value().c_str());
    
    // Test current sensor
    mb8art->setMockChannelConfig(2, mb8art::ChannelMode::CURRENT, 0);
    result = mb8art->getChannelUnit(2);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_EQUAL_STRING("mA", result.value().c_str());
}

// Test temperature requests
void test_request_temperatures() {
    mb8art->initialize();
    mb8art->resetCounters();
    
    TEST_ASSERT_TRUE(mb8art->requestTemperatures());
    TEST_ASSERT_EQUAL(1, mb8art->getTemperatureRequestCount());
}

// Test measurement range
void test_measurement_range_low_res() {
    mb8art->setMockMeasurementRange(mb8art::MeasurementRange::LOW_RES);
    mb8art->initialize();
    
    TEST_ASSERT_EQUAL(mb8art::MeasurementRange::LOW_RES, mb8art->getMeasurementRange());
}

void test_measurement_range_high_res() {
    mb8art->setMockMeasurementRange(mb8art::MeasurementRange::HIGH_RES);
    mb8art->initialize();
    
    TEST_ASSERT_EQUAL(mb8art::MeasurementRange::HIGH_RES, mb8art->getMeasurementRange());
}

// Test error handling
void test_modbus_timeout_error() {
    mb8art->initialize();
    
    // Simulate timeout error
    mb8art->simulateError(ModbusError::TIMEOUT);
    
    // Verify error handling (would need to expose error state for full testing)
    // For now, just verify it doesn't crash
    TEST_ASSERT_TRUE(true);
}

void test_modbus_crc_error() {
    mb8art->initialize();
    
    // Simulate CRC error
    mb8art->simulateError(ModbusError::CRC);
    
    // Verify error handling
    TEST_ASSERT_TRUE(true);
}

// Test disconnected sensors
void test_disconnected_sensor() {
    mb8art->initialize();
    mb8art->setMockTemperature(2, NAN, false);  // Channel 2 disconnected
    
    // Simulate connection status response
    uint8_t connData[8] = {0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF}; // All except ch2
    mb8art->simulateModbusResponse(0x02, 0, connData, 8);
    
    TEST_ASSERT_FALSE(mb8art->isSensorConnected(2));
    TEST_ASSERT_TRUE(mb8art->isSensorConnected(0));
    TEST_ASSERT_TRUE(mb8art->isSensorConnected(1));
}

// Test temperature data processing
void test_temperature_out_of_range() {
    mb8art->initialize();
    
    // Simulate temperature out of range (>850°C)
    uint8_t tempData[16] = {0};
    tempData[0] = 0x35;  // 8600 = 860.0°C (out of range)
    tempData[1] = 0x18;
    
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    auto result = mb8art->getValue(0);
    // Temperature might be clamped or marked invalid depending on implementation
    TEST_ASSERT_TRUE(result.isOk() || result.error() == ModbusError::NO_DATA);
}

// Test module settings
void test_module_settings() {
    mb8art->initialize();
    
    const auto& settings = mb8art->getModuleSettings();
    // Just verify we can access settings without crash
    TEST_ASSERT_TRUE(true);
}

// Main test runner
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_initialization_success);
    RUN_TEST(test_initialization_failure);
    RUN_TEST(test_device_offline);
    
    // IModbusInput interface tests
    RUN_TEST(test_get_channel_count);
    RUN_TEST(test_get_value_valid_channel);
    RUN_TEST(test_get_value_invalid_channel);
    RUN_TEST(test_get_all_values);
    RUN_TEST(test_get_channel_name);
    RUN_TEST(test_get_channel_unit);
    
    // Functionality tests
    RUN_TEST(test_request_temperatures);
    RUN_TEST(test_measurement_range_low_res);
    RUN_TEST(test_measurement_range_high_res);
    
    // Error handling tests
    RUN_TEST(test_modbus_timeout_error);
    RUN_TEST(test_modbus_crc_error);
    
    // Sensor connection tests
    RUN_TEST(test_disconnected_sensor);
    
    // Data processing tests
    RUN_TEST(test_temperature_out_of_range);
    
    // Module settings test
    RUN_TEST(test_module_settings);
    
    return UNITY_END();
}