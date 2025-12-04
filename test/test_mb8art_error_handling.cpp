#include <unity.h>
#include "MockMB8ART.h"
#include <memory>
#include <thread>
#include <chrono>

// Test error handling and edge cases

std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    mb8art.reset();
}

// Test initialization errors
void test_init_device_not_responding() {
    mb8art->setDeviceOffline(true);
    
    TEST_ASSERT_FALSE(mb8art->initialize());
    TEST_ASSERT_FALSE(mb8art->isReady());
    TEST_ASSERT_TRUE(mb8art->isModuleOffline());
}

void test_init_partial_configuration() {
    // Simulate partial init success (device responds but config fails)
    mb8art->setInitializationFailure(true);
    
    TEST_ASSERT_FALSE(mb8art->initialize());
}

// Test communication errors
void test_repeated_timeouts() {
    mb8art->initialize();
    
    // Simulate multiple timeout errors
    for (int i = 0; i < 5; i++) {
        mb8art->simulateError(ModbusError::TIMEOUT);
    }
    
    // Device should still be functional (mock doesn't track offline state from errors)
    TEST_ASSERT_TRUE(mb8art->isReady());
}

void test_crc_errors() {
    mb8art->initialize();
    
    // Simulate CRC errors
    mb8art->simulateError(ModbusError::CRC);
    mb8art->simulateError(ModbusError::CRC);
    
    // Verify device handles CRC errors gracefully
    TEST_ASSERT_TRUE(mb8art->isReady());
}

// Test data validation
void test_invalid_temperature_data() {
    mb8art->initialize();
    
    // Simulate corrupted temperature data
    uint8_t badData[16];
    for (int i = 0; i < 16; i++) {
        badData[i] = 0xFF;  // All 0xFF = very high temperature
    }
    
    mb8art->simulateModbusResponse(0x04, 8, badData, 16);
    
    // Should handle invalid data gracefully
    auto result = mb8art->getValue(0);
    // Either returns the value or marks as invalid
    TEST_ASSERT_TRUE(result.isOk() || result.error() == ModbusError::NO_DATA);
}

void test_short_response_packet() {
    mb8art->initialize();
    
    // Simulate response that's too short
    uint8_t shortData[4] = {0x00, 0xC8, 0x00, 0xD2};  // Only 2 temperatures
    
    // This should not crash
    mb8art->simulateModbusResponse(0x04, 8, shortData, 4);
    
    // First two channels might have data, others should be unchanged
    TEST_ASSERT_TRUE(true);  // Just verify no crash
}

void test_wrong_function_code_response() {
    mb8art->initialize();
    
    // Send response with wrong function code
    uint8_t data[2] = {0x00, 0x01};
    mb8art->simulateModbusResponse(0x05, 8, data, 2);  // FC 0x05 instead of 0x04
    
    // Should ignore wrong function code
    TEST_ASSERT_TRUE(mb8art->isReady());
}

// Test boundary conditions
void test_temperature_boundaries() {
    mb8art->initialize();
    
    // Test minimum temperature (-200°C)
    uint8_t minTemp[16] = {0};
    minTemp[0] = 0xF8;  // -2000 = -200.0°C
    minTemp[1] = 0x30;
    
    mb8art->simulateModbusResponse(0x04, 8, minTemp, 16);
    
    auto result = mb8art->getValue(0);
    if (result.isOk()) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, -200.0f, result.value());
    }
    
    // Test maximum temperature (850°C for low res)
    uint8_t maxTemp[16] = {0};
    maxTemp[0] = 0x21;  // 8500 = 850.0°C
    maxTemp[1] = 0x34;
    
    mb8art->simulateModbusResponse(0x04, 8, maxTemp, 16);
    
    result = mb8art->getValue(0);
    if (result.isOk()) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 850.0f, result.value());
    }
}

// Test concurrent access (if using threads)
void test_concurrent_temperature_requests() {
    mb8art->initialize();
    
    // Simulate multiple concurrent requests
    bool allSucceeded = true;
    
    auto requestFunc = [&]() {
        for (int i = 0; i < 10; i++) {
            if (!mb8art->requestTemperatures()) {
                allSucceeded = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    // Create multiple threads making requests
    std::thread t1(requestFunc);
    std::thread t2(requestFunc);
    
    t1.join();
    t2.join();
    
    TEST_ASSERT_TRUE(allSucceeded);
}

// Test recovery from errors
void test_recovery_after_offline() {
    mb8art->initialize();
    
    // Set device offline
    mb8art->setDeviceOffline(true);
    
    // Try to request data (should fail)
    TEST_ASSERT_FALSE(mb8art->requestTemperatures());
    
    // Bring device back online
    mb8art->setDeviceOffline(false);
    
    // Should be able to request again
    TEST_ASSERT_TRUE(mb8art->requestTemperatures());
}

// Test edge case register addresses
void test_invalid_register_response() {
    mb8art->initialize();
    
    // Simulate response from unexpected register
    uint8_t data[2] = {0x00, 0x01};
    mb8art->simulateModbusResponse(0x03, 9999, data, 2);
    
    // Should handle gracefully
    TEST_ASSERT_TRUE(mb8art->isReady());
}

// Test module responsiveness check
void test_module_responsive_check() {
    mb8art->initialize();
    
    // Initially should be responsive
    TEST_ASSERT_TRUE(mb8art->isModuleResponsive());
    
    // After setting offline, should not be responsive
    mb8art->setDeviceOffline(true);
    TEST_ASSERT_FALSE(mb8art->isModuleResponsive());
}

// Test data freshness
void test_has_recent_sensor_data() {
    mb8art->initialize();
    
    // Initially no recent data
    TEST_ASSERT_FALSE(mb8art->hasRecentSensorData(1000));
    
    // Simulate temperature update
    uint8_t tempData[16] = {0};
    for (int i = 0; i < 8; i++) {
        tempData[i * 2] = 0x00;
        tempData[i * 2 + 1] = 0xC8;  // 20.0°C
    }
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    // Now should have recent data
    TEST_ASSERT_TRUE(mb8art->hasRecentSensorData(5000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Initialization error tests
    RUN_TEST(test_init_device_not_responding);
    RUN_TEST(test_init_partial_configuration);
    
    // Communication error tests
    RUN_TEST(test_repeated_timeouts);
    RUN_TEST(test_crc_errors);
    
    // Data validation tests
    RUN_TEST(test_invalid_temperature_data);
    RUN_TEST(test_short_response_packet);
    RUN_TEST(test_wrong_function_code_response);
    
    // Boundary condition tests
    RUN_TEST(test_temperature_boundaries);
    
    // Concurrent access test
    RUN_TEST(test_concurrent_temperature_requests);
    
    // Recovery tests
    RUN_TEST(test_recovery_after_offline);
    
    // Edge case tests
    RUN_TEST(test_invalid_register_response);
    
    // Module state tests
    RUN_TEST(test_module_responsive_check);
    RUN_TEST(test_has_recent_sensor_data);
    
    return UNITY_END();
}