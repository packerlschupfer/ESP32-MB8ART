#include <unity.h>
#include "MockMB8ART.h"
#include <memory>

// Test configuration functionality

std::unique_ptr<MockMB8ART> mb8art;

void setUp() {
    mb8art = std::make_unique<MockMB8ART>(0x01);
}

void tearDown() {
    mb8art.reset();
}

// Test channel configuration
void test_configure_single_channel() {
    mb8art->initialize();
    
    // Configure channel 0 as PT1000
    auto result = mb8art->configureChannelMode(0, 
        static_cast<uint16_t>(mb8art::ChannelMode::PT_INPUT) | 
        (static_cast<uint16_t>(mb8art::PTType::PT1000) << 8));
    
    TEST_ASSERT_EQUAL(mb8art::SensorErrorCode::SUCCESS, result);
}

void test_configure_all_channels() {
    mb8art->initialize();
    
    // Configure all channels as thermocouples
    auto result = mb8art->configureAllChannels(
        mb8art::ChannelMode::THERMOCOUPLE,
        static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_K)
    );
    
    TEST_ASSERT_EQUAL(mb8art::SensorErrorCode::SUCCESS, result);
}

void test_configure_channel_range() {
    mb8art->initialize();
    
    // Configure channels 2-5 as voltage inputs
    auto result = mb8art->configureChannelRange(2, 5,
        mb8art::ChannelMode::VOLTAGE,
        static_cast<uint16_t>(mb8art::VoltageRange::V_1)
    );
    
    TEST_ASSERT_EQUAL(mb8art::SensorErrorCode::SUCCESS, result);
}

void test_configure_invalid_channel() {
    mb8art->initialize();
    
    // Try to configure invalid channel
    auto result = mb8art->configureChannelMode(8, 0x0201);
    
    TEST_ASSERT_EQUAL(mb8art::SensorErrorCode::INVALID_INDEX, result);
}

// Test measurement range configuration
void test_configure_measurement_range() {
    mb8art->initialize();
    
    // Switch to high resolution mode
    auto result = mb8art->configureMeasurementRange(mb8art::MeasurementRange::HIGH_RES);
    
    TEST_ASSERT_EQUAL(mb8art::SensorErrorCode::SUCCESS, result);
}

// Test factory reset
void test_factory_reset() {
    mb8art->initialize();
    
    bool result = mb8art->setFactoryReset();
    
    // Mock should succeed
    TEST_ASSERT_TRUE(result);
}

// Test RS485 configuration
void test_set_address() {
    mb8art->initialize();
    
    bool result = mb8art->setAddress(0x02);
    
    TEST_ASSERT_TRUE(result);
}

void test_set_baud_rate() {
    mb8art->initialize();
    
    bool result = mb8art->setBaudRate(4);  // 19200 baud
    
    TEST_ASSERT_TRUE(result);
}

void test_set_parity() {
    mb8art->initialize();
    
    bool result = mb8art->setParity(1);  // Even parity
    
    TEST_ASSERT_TRUE(result);
}

// Test batch configuration
void test_batch_read_config() {
    mb8art->initialize();
    
    bool result = mb8art->batchReadAllConfig();
    
    // Should succeed in mock
    TEST_ASSERT_TRUE(result);
}

// Test channel mode validation
void test_deactivated_channel() {
    mb8art->initialize();
    mb8art->setMockChannelConfig(3, mb8art::ChannelMode::DEACTIVATED, 0);
    
    // Deactivated channel should return NaN
    auto result = mb8art->getValue(3);
    
    TEST_ASSERT_FALSE(result.isOk());
}

// Test mixed channel types
void test_mixed_channel_configuration() {
    mb8art->initialize();
    
    // Configure different channel types
    mb8art->setMockChannelConfig(0, mb8art::ChannelMode::PT_INPUT, 
                                 static_cast<uint16_t>(mb8art::PTType::PT100));
    mb8art->setMockChannelConfig(1, mb8art::ChannelMode::THERMOCOUPLE,
                                 static_cast<uint16_t>(mb8art::ThermocoupleType::TYPE_J));
    mb8art->setMockChannelConfig(2, mb8art::ChannelMode::VOLTAGE,
                                 static_cast<uint16_t>(mb8art::VoltageRange::MV_100));
    mb8art->setMockChannelConfig(3, mb8art::ChannelMode::CURRENT,
                                 static_cast<uint16_t>(mb8art::CurrentRange::MA_4_TO_20));
    
    // Verify each channel has correct unit
    auto result = mb8art->getChannelUnit(0);
    TEST_ASSERT_EQUAL_STRING("°C", result.value().c_str());
    
    result = mb8art->getChannelUnit(1);
    TEST_ASSERT_EQUAL_STRING("°C", result.value().c_str());
    
    result = mb8art->getChannelUnit(2);
    TEST_ASSERT_EQUAL_STRING("V", result.value().c_str());
    
    result = mb8art->getChannelUnit(3);
    TEST_ASSERT_EQUAL_STRING("mA", result.value().c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Channel configuration tests
    RUN_TEST(test_configure_single_channel);
    RUN_TEST(test_configure_all_channels);
    RUN_TEST(test_configure_channel_range);
    RUN_TEST(test_configure_invalid_channel);
    
    // Measurement range tests
    RUN_TEST(test_configure_measurement_range);
    
    // Factory reset test
    RUN_TEST(test_factory_reset);
    
    // RS485 configuration tests
    RUN_TEST(test_set_address);
    RUN_TEST(test_set_baud_rate);
    RUN_TEST(test_set_parity);
    
    // Batch configuration test
    RUN_TEST(test_batch_read_config);
    
    // Channel validation tests
    RUN_TEST(test_deactivated_channel);
    RUN_TEST(test_mixed_channel_configuration);
    
    return UNITY_END();
}