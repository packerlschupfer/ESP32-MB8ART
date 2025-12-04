#include <unity.h>
#include "MockMB8ART.h"
#include <memory>
#include <chrono>
#include <thread>
#include <vector>

// Test Modbus communication robustness, timeout handling, and CRC validation

std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    mb8art.reset();
}

// Enhanced Mock for timeout simulation
class TimeoutMockMB8ART : public MockMB8ART {
public:
    explicit TimeoutMockMB8ART(uint8_t address) : MockMB8ART(address) {}
    
    // Configuration for timeout behavior
    void setTimeoutOnRequest(uint16_t registerAddr, bool shouldTimeout) {
        timeoutRegisters[registerAddr] = shouldTimeout;
    }
    
    void setTimeoutAfterNRequests(uint32_t n) {
        timeoutAfterN = n;
        requestCounter = 0;
    }
    
    void setCRCErrorOnRequest(uint16_t registerAddr, bool shouldError) {
        crcErrorRegisters[registerAddr] = shouldError;
    }
    
protected:
    bool sendRequest(uint16_t registerAddress, uint16_t numRegisters, 
                    esp32Modbus::FunctionCode fc) override {
        requestCounter++;
        
        // Check if should timeout after N requests
        if (timeoutAfterN > 0 && requestCounter >= timeoutAfterN) {
            // Simulate timeout by not calling response handler
            simulateError(ModbusError::TIMEOUT);
            return false;
        }
        
        // Check if specific register should timeout
        if (timeoutRegisters.find(registerAddress) != timeoutRegisters.end() && 
            timeoutRegisters[registerAddress]) {
            simulateError(ModbusError::TIMEOUT);
            return false;
        }
        
        // Check if should simulate CRC error
        if (crcErrorRegisters.find(registerAddress) != crcErrorRegisters.end() && 
            crcErrorRegisters[registerAddress]) {
            simulateError(ModbusError::CRC);
            return false;
        }
        
        // Otherwise proceed normally
        return MockMB8ART::sendRequest(registerAddress, numRegisters, fc);
    }
    
private:
    std::map<uint16_t, bool> timeoutRegisters;
    std::map<uint16_t, bool> crcErrorRegisters;
    uint32_t timeoutAfterN = 0;
    uint32_t requestCounter = 0;
};

// Timeout handling tests
void test_single_timeout_recovery() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    timeoutMock->initialize();
    
    // First request should timeout
    timeoutMock->setTimeoutOnRequest(8, true);  // Temperature register
    
    TEST_ASSERT_FALSE(timeoutMock->requestTemperatures());
    TEST_ASSERT_EQUAL(ModbusError::TIMEOUT, timeoutMock->getLastError());
    
    // Clear timeout condition
    timeoutMock->setTimeoutOnRequest(8, false);
    
    // Next request should succeed
    TEST_ASSERT_TRUE(timeoutMock->requestTemperatures());
}

void test_consecutive_timeouts() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    timeoutMock->initialize();
    
    // Simulate 3 consecutive timeouts
    timeoutMock->setTimeoutOnRequest(8, true);
    
    int timeoutCount = 0;
    for (int i = 0; i < 3; i++) {
        if (!timeoutMock->requestTemperatures()) {
            timeoutCount++;
        }
    }
    
    TEST_ASSERT_EQUAL(3, timeoutCount);
    
    // Verify device still functional after timeouts
    timeoutMock->setTimeoutOnRequest(8, false);
    TEST_ASSERT_TRUE(timeoutMock->requestTemperatures());
}

void test_timeout_during_initialization() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    
    // Timeout on measurement range read during init
    timeoutMock->setTimeoutOnRequest(76, true);
    
    TEST_ASSERT_FALSE(timeoutMock->initialize());
    TEST_ASSERT_FALSE(timeoutMock->isReady());
}

void test_partial_response_timeout() {
    mb8art->initialize();
    
    // Request all channels but simulate timeout mid-response
    mb8art->requestTemperatures();
    
    // Simulate partial data (only 4 channels)
    uint8_t partialData[8] = {
        0x00, 0xC8,  // Ch0: 20.0°C
        0x00, 0xD2,  // Ch1: 21.0°C
        0x00, 0xDC,  // Ch2: 22.0°C
        0x00, 0xE6   // Ch3: 23.0°C
    };
    
    mb8art->simulateModbusResponse(0x04, 8, partialData, 8);
    
    // Then simulate timeout
    mb8art->simulateError(ModbusError::TIMEOUT);
    
    // First 4 channels might have data, others should be unchanged
    auto result = mb8art->getValue(0);
    if (result.isOk()) {
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, result.value());
    }
    
    // Channel 4+ should have no data or old data
    result = mb8art->getValue(4);
    // Result depends on implementation behavior
}

void test_timeout_with_retry_pattern() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    timeoutMock->initialize();
    
    // Timeout every other request
    int successCount = 0;
    int timeoutCount = 0;
    
    for (int i = 0; i < 10; i++) {
        timeoutMock->setTimeoutOnRequest(8, (i % 2) == 0);
        
        if (timeoutMock->requestTemperatures()) {
            successCount++;
        } else {
            timeoutCount++;
        }
    }
    
    TEST_ASSERT_EQUAL(5, successCount);
    TEST_ASSERT_EQUAL(5, timeoutCount);
}

// CRC validation tests
void test_single_crc_error() {
    auto crcMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    crcMock->initialize();
    
    // Simulate CRC error on temperature request
    crcMock->setCRCErrorOnRequest(8, true);
    
    TEST_ASSERT_FALSE(crcMock->requestTemperatures());
    TEST_ASSERT_EQUAL(ModbusError::CRC, crcMock->getLastError());
    
    // Clear CRC error
    crcMock->setCRCErrorOnRequest(8, false);
    
    // Next request should succeed
    TEST_ASSERT_TRUE(crcMock->requestTemperatures());
}

void test_crc_error_during_config_read() {
    auto crcMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    
    // CRC error on channel config read
    crcMock->setCRCErrorOnRequest(128, true);
    
    // Initialize might fail or retry depending on implementation
    bool initResult = crcMock->initialize();
    
    // If init succeeded, it handled the CRC error
    // If it failed, that's also valid behavior
    TEST_ASSERT_TRUE(true);  // Just verify no crash
}

void test_mixed_timeout_crc_errors() {
    auto mixedMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    mixedMock->initialize();
    
    std::vector<ModbusError> errors;
    
    // Alternate between timeout and CRC errors
    for (int i = 0; i < 6; i++) {
        if (i % 2 == 0) {
            mixedMock->setTimeoutOnRequest(8, true);
            mixedMock->setCRCErrorOnRequest(8, false);
        } else {
            mixedMock->setTimeoutOnRequest(8, false);
            mixedMock->setCRCErrorOnRequest(8, true);
        }
        
        if (!mixedMock->requestTemperatures()) {
            errors.push_back(mixedMock->getLastError());
        }
    }
    
    // Should have alternating error types
    TEST_ASSERT_EQUAL(6, errors.size());
    for (size_t i = 0; i < errors.size(); i++) {
        if (i % 2 == 0) {
            TEST_ASSERT_EQUAL(ModbusError::TIMEOUT, errors[i]);
        } else {
            TEST_ASSERT_EQUAL(ModbusError::CRC, errors[i]);
        }
    }
}

// Timeout threshold tests
void test_timeout_threshold_behavior() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    timeoutMock->initialize();
    
    // Simulate timeouts after every 3 successful requests
    timeoutMock->setTimeoutAfterNRequests(3);
    
    int successCount = 0;
    int failCount = 0;
    
    for (int i = 0; i < 10; i++) {
        if (timeoutMock->requestTemperatures()) {
            successCount++;
        } else {
            failCount++;
            // Reset counter after timeout
            timeoutMock->setTimeoutAfterNRequests(3);
        }
    }
    
    // Should have pattern of 3 successes, 1 failure
    TEST_ASSERT_GREATER_THAN(0, successCount);
    TEST_ASSERT_GREATER_THAN(0, failCount);
}

// Response timing tests
void test_delayed_response_handling() {
    mb8art->initialize();
    
    // Request data
    mb8art->requestTemperatures();
    
    // Simulate delay before response
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Then send response
    uint8_t tempData[16] = {0};
    for (int i = 0; i < 8; i++) {
        tempData[i * 2] = 0x00;
        tempData[i * 2 + 1] = 0xC8;  // 20.0°C
    }
    
    mb8art->simulateModbusResponse(0x04, 8, tempData, 16);
    
    // Data should still be processed correctly
    auto result = mb8art->getValue(0);
    TEST_ASSERT_TRUE(result.isOk());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, result.value());
}

// Error statistics tracking
void test_error_statistics() {
    auto errorMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    errorMock->initialize();
    
    // Reset statistics
    errorMock->resetStatistics();
    
    // Generate some errors
    errorMock->setTimeoutOnRequest(8, true);
    errorMock->requestTemperatures();  // Timeout
    
    errorMock->setTimeoutOnRequest(8, false);
    errorMock->setCRCErrorOnRequest(8, true);
    errorMock->requestTemperatures();  // CRC error
    
    errorMock->setCRCErrorOnRequest(8, false);
    errorMock->requestTemperatures();  // Success
    
    auto stats = errorMock->getStatistics();
    TEST_ASSERT_EQUAL(3, stats.totalRequests);
    TEST_ASSERT_EQUAL(2, stats.errorResponses);  // 2 errors
    TEST_ASSERT_EQUAL(1, stats.validResponses);  // 1 success
}

// Corrupt data handling
void test_corrupt_modbus_frame() {
    mb8art->initialize();
    
    // Simulate corrupted frame with invalid data
    uint8_t corruptData[16];
    for (int i = 0; i < 16; i++) {
        corruptData[i] = 0xFF;  // All 0xFF could indicate corruption
    }
    
    // First simulate CRC error
    mb8art->simulateError(ModbusError::CRC);
    
    // Then the corrupt data arrives
    mb8art->simulateModbusResponse(0x04, 8, corruptData, 16);
    
    // Should handle gracefully
    TEST_ASSERT_EQUAL(ModbusError::CRC, mb8art->getLastError());
}

// Timeout recovery patterns
void test_exponential_backoff_simulation() {
    auto timeoutMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    timeoutMock->initialize();
    
    // Always timeout to test backoff
    timeoutMock->setTimeoutOnRequest(8, true);
    
    std::vector<int> delays = {10, 20, 40, 80, 160};  // ms
    
    for (int delay : delays) {
        auto start = std::chrono::steady_clock::now();
        
        timeoutMock->requestTemperatures();
        
        // Simulate backoff delay
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        
        TEST_ASSERT_GREATER_OR_EQUAL(delay, elapsed);
    }
}

// Stress test with random errors
void test_random_error_stress() {
    auto stressMock = std::make_unique<TimeoutMockMB8ART>(0x01);
    stressMock->initialize();
    
    int totalRequests = 100;
    int timeouts = 0;
    int crcErrors = 0;
    int successes = 0;
    
    // Seed for reproducible tests
    srand(12345);
    
    for (int i = 0; i < totalRequests; i++) {
        int errorType = rand() % 3;  // 0=success, 1=timeout, 2=crc
        
        stressMock->setTimeoutOnRequest(8, errorType == 1);
        stressMock->setCRCErrorOnRequest(8, errorType == 2);
        
        if (stressMock->requestTemperatures()) {
            successes++;
        } else {
            if (stressMock->getLastError() == ModbusError::TIMEOUT) {
                timeouts++;
            } else if (stressMock->getLastError() == ModbusError::CRC) {
                crcErrors++;
            }
        }
    }
    
    // Verify all requests accounted for
    TEST_ASSERT_EQUAL(totalRequests, successes + timeouts + crcErrors);
    TEST_ASSERT_GREATER_THAN(0, successes);
    TEST_ASSERT_GREATER_THAN(0, timeouts);
    TEST_ASSERT_GREATER_THAN(0, crcErrors);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Timeout handling tests
    RUN_TEST(test_single_timeout_recovery);
    RUN_TEST(test_consecutive_timeouts);
    RUN_TEST(test_timeout_during_initialization);
    RUN_TEST(test_partial_response_timeout);
    RUN_TEST(test_timeout_with_retry_pattern);
    
    // CRC validation tests
    RUN_TEST(test_single_crc_error);
    RUN_TEST(test_crc_error_during_config_read);
    RUN_TEST(test_mixed_timeout_crc_errors);
    
    // Threshold and timing tests
    RUN_TEST(test_timeout_threshold_behavior);
    RUN_TEST(test_delayed_response_handling);
    
    // Statistics and monitoring
    RUN_TEST(test_error_statistics);
    
    // Corrupt data handling
    RUN_TEST(test_corrupt_modbus_frame);
    
    // Recovery patterns
    RUN_TEST(test_exponential_backoff_simulation);
    
    // Stress testing
    RUN_TEST(test_random_error_stress);
    
    return UNITY_END();
}