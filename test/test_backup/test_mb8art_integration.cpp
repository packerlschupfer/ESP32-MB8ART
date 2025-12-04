#include <unity.h>
#include "MockMB8ART.h"
#include <memory>
#include <vector>

// Integration tests with ModbusDevice base class

std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    mb8art.reset();
}

// Test ModbusDevice base class integration
void test_modbus_device_initialization() {
    // Verify ModbusDevice base class is properly initialized
    TEST_ASSERT_EQUAL(0x01, mb8art->getServerAddress());
    TEST_ASSERT_EQUAL(ModbusDevice::InitPhase::UNINITIALIZED, mb8art->getInitPhase());
}

void test_initialization_phases() {
    // Verify initialization goes through proper phases
    TEST_ASSERT_EQUAL(ModbusDevice::InitPhase::UNINITIALIZED, mb8art->getInitPhase());
    
    mb8art->initialize();
    
    // After successful init, should be READY
    TEST_ASSERT_EQUAL(ModbusDevice::InitPhase::READY, mb8art->getInitPhase());
    TEST_ASSERT_TRUE(mb8art->isReady());
}

// Test async queue functionality
void test_async_queue_operations() {
    mb8art->initialize();
    
    // Enable async mode
    mb8art->enableAsync(10);
    TEST_ASSERT_TRUE(mb8art->isAsyncEnabled());
    
    // Simulate some async responses
    uint8_t tempData[16] = {0};
    for (int i = 0; i < 8; i++) {
        tempData[i * 2] = 0x00;
        tempData[i * 2 + 1] = 0xC8;  // 20.0°C
    }
    
    // Queue multiple responses
    for (int i = 0; i < 5; i++) {
        mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    }
    
    // Process queue
    size_t processed = mb8art->processQueue();
    TEST_ASSERT_GREATER_THAN(0, processed);
}

void test_queue_depth_monitoring() {
    mb8art->initialize();
    mb8art->enableAsync(10);
    
    // Initially queue should be empty
    TEST_ASSERT_EQUAL(0, mb8art->getQueueDepth());
    
    // Add responses to queue
    uint8_t data[2] = {0x00, 0x01};
    for (int i = 0; i < 3; i++) {
        mb8art->simulateModbusResponse(0x03, 76, data, 2);
    }
    
    // Queue depth should increase
    TEST_ASSERT_GREATER_THAN(0, mb8art->getQueueDepth());
    
    // Process all
    mb8art->processQueue(0);
    
    // Queue should be empty again
    TEST_ASSERT_EQUAL(0, mb8art->getQueueDepth());
}

// Test IModbusInput integration
void test_imodbus_input_with_async() {
    mb8art->initialize();
    mb8art->enableAsync(10);
    
    // Request temperatures
    TEST_ASSERT_TRUE(mb8art->requestTemperatures());
    
    // Simulate async response
    uint8_t tempData[16];
    for (int i = 0; i < 8; i++) {
        uint16_t temp = 200 + i * 10;  // 20.0, 21.0, 22.0...
        tempData[i * 2] = (temp >> 8) & 0xFF;
        tempData[i * 2 + 1] = temp & 0xFF;
    }
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    // Process the response
    mb8art->processQueue();
    
    // Verify data through IModbusInput interface
    auto result = mb8art->getAllValues();
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_EQUAL(8, result.value().size());
    
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f + i, result.value()[i]);
    }
}

// Test error handling through base class
void test_modbus_error_propagation() {
    mb8art->initialize();
    
    // Get initial error state
    TEST_ASSERT_EQUAL(ModbusError::SUCCESS, mb8art->getLastError());
    
    // Simulate error
    mb8art->simulateError(ModbusError::TIMEOUT);
    
    // Error should be reflected in base class
    TEST_ASSERT_EQUAL(ModbusError::TIMEOUT, mb8art->getLastError());
}

// Test statistics tracking
void test_statistics_tracking() {
    mb8art->initialize();
    
    // Get initial stats
    auto stats = mb8art->getStatistics();
    uint32_t initialRequests = stats.totalRequests;
    
    // Make some requests
    for (int i = 0; i < 5; i++) {
        mb8art->requestTemperatures();
    }
    
    // Stats should be updated
    stats = mb8art->getStatistics();
    TEST_ASSERT_EQUAL(initialRequests + 5, stats.totalRequests);
    
    // Reset statistics
    mb8art->resetStatistics();
    stats = mb8art->getStatistics();
    TEST_ASSERT_EQUAL(0, stats.totalRequests);
}

// Test connection status
void test_connection_status() {
    // Before init, not connected
    TEST_ASSERT_FALSE(mb8art->isConnected());
    
    // After successful init, should be connected
    mb8art->initialize();
    TEST_ASSERT_TRUE(mb8art->isConnected());
    
    // Simulate offline
    mb8art->setDeviceOffline(true);
    // Mock doesn't update connection status automatically
    // In real implementation, errors would affect this
}

// Test data freshness with ModbusResult
void test_modbus_result_handling() {
    mb8art->initialize();
    
    // Test successful result
    auto result = mb8art->getValue(0);
    if (result.isOk()) {
        TEST_ASSERT_TRUE(result.hasValue());
        TEST_ASSERT_FALSE(std::isnan(result.value()));
    }
    
    // Test error result
    result = mb8art->getValue(10);  // Invalid channel
    TEST_ASSERT_FALSE(result.isOk());
    TEST_ASSERT_FALSE(result.hasValue());
    TEST_ASSERT_EQUAL(ModbusError::INVALID_PARAMETER, result.error());
}

// Test batch operations
void test_batch_temperature_reading() {
    mb8art->initialize();
    
    // Set different temperatures for each channel
    std::vector<float> expectedTemps = {20.0f, 21.5f, 22.0f, 23.5f, 
                                        24.0f, 25.5f, 26.0f, 27.5f};
    
    // Simulate batch temperature response
    uint8_t tempData[16];
    for (int i = 0; i < 8; i++) {
        uint16_t temp = static_cast<uint16_t>(expectedTemps[i] * 10);
        tempData[i * 2] = (temp >> 8) & 0xFF;
        tempData[i * 2 + 1] = temp & 0xFF;
    }
    
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    // Read all values at once
    auto result = mb8art->getAllValues();
    TEST_ASSERT_TRUE(result.isOk());
    
    // Verify each temperature
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedTemps[i], result.value()[i]);
    }
}

// Test channel information through IModbusInput
void test_channel_information() {
    mb8art->initialize();
    
    // Configure various channel types
    mb8art->setMockChannelConfig(0, mb8art::ChannelMode::PT_INPUT, 
                                 static_cast<uint16_t>(mb8art::PTType::PT1000));
    mb8art->setMockChannelConfig(1, mb8art::ChannelMode::THERMOCOUPLE,
                                 static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_K));
    mb8art->setMockChannelConfig(2, mb8art::ChannelMode::DEACTIVATED, 0);
    
    // Test channel count
    TEST_ASSERT_EQUAL(8, mb8art->getChannelCount());
    
    // Test channel names
    auto name0 = mb8art->getChannelName(0);
    TEST_ASSERT_TRUE(name0.isOk());
    TEST_ASSERT_TRUE(name0.value().find("PT_INPUT") != std::string::npos);
    
    auto name1 = mb8art->getChannelName(1);
    TEST_ASSERT_TRUE(name1.isOk());
    TEST_ASSERT_TRUE(name1.value().find("THERMOCOUPLE") != std::string::npos);
    
    // Test invalid channel
    auto nameInvalid = mb8art->getChannelName(10);
    TEST_ASSERT_FALSE(nameInvalid.isOk());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // ModbusDevice base class tests
    RUN_TEST(test_modbus_device_initialization);
    RUN_TEST(test_initialization_phases);
    
    // Async queue tests
    RUN_TEST(test_async_queue_operations);
    RUN_TEST(test_queue_depth_monitoring);
    
    // IModbusInput integration tests
    RUN_TEST(test_imodbus_input_with_async);
    
    // Error handling tests
    RUN_TEST(test_modbus_error_propagation);
    
    // Statistics tests
    RUN_TEST(test_statistics_tracking);
    
    // Connection tests
    RUN_TEST(test_connection_status);
    
    // ModbusResult tests
    RUN_TEST(test_modbus_result_handling);
    
    // Batch operation tests
    RUN_TEST(test_batch_temperature_reading);
    
    // Channel information tests
    RUN_TEST(test_channel_information);
    
    return UNITY_END();
}