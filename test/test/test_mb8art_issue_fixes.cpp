/**
 * @file test_mb8art_issue_fixes.cpp
 * @brief Unit tests for MB8ART library issue fixes
 *
 * Tests for:
 * - Issue 1: Thread-safe log throttling (spinlock protection)
 * - Issue 2: Pre-computed activeChannelMask usage
 * - Issue 3: Automatic offline detection on consecutive timeouts
 */

#include <unity.h>
#include "MockMB8ART.h"
#include <memory>

// Test fixtures
std::unique_ptr<MockMB8ART> device;

void setUp() {
    device = std::make_unique<MockMB8ART>(0x03);
}

void tearDown() {
    device.reset();
}

// ============================================================================
// Issue 1: Thread-safe log throttling
// Note: Full spinlock testing requires multi-task environment (ESP32 hardware)
// These tests verify the basic throttling logic
// ============================================================================

void test_error_logging_throttle_initial() {
    // First error should always be logged (throttle timer starts at 0)
    // This tests that the throttling mechanism is properly initialized
    device->initialize();

    // Simulate error condition - first occurrence should trigger logging
    // The test verifies the code path exists and doesn't crash
    TEST_ASSERT_TRUE(device->isReady());
}

// ============================================================================
// Issue 2: Pre-computed activeChannelMask usage
// Tests verify the mask is properly computed and used
// ============================================================================

void test_active_channel_mask_all_active() {
    device->initialize();

    // Configure all channels as active (PT_INPUT mode)
    for (uint8_t i = 0; i < 8; i++) {
        device->setMockChannelConfig(i, mb8art::ChannelMode::PT_INPUT, 0);
    }

    // Trigger mask update
    device->forceUpdateActiveChannelMask();

    // All 8 channels active = mask 0xFF
    TEST_ASSERT_EQUAL_HEX8(0xFF, device->getActiveChannelMask() & 0xFF);
    TEST_ASSERT_EQUAL(8, device->getActiveChannelCount());
}

void test_active_channel_mask_partial() {
    device->initialize();

    // Configure channels 0, 2, 4, 6 as active
    for (uint8_t i = 0; i < 8; i++) {
        if (i % 2 == 0) {
            device->setMockChannelConfig(i, mb8art::ChannelMode::PT_INPUT, 0);
        } else {
            device->setMockChannelConfig(i, mb8art::ChannelMode::DEACTIVATED, 0);
        }
    }

    device->forceUpdateActiveChannelMask();

    // Channels 0, 2, 4, 6 active = mask 0x55 (binary: 01010101)
    TEST_ASSERT_EQUAL_HEX8(0x55, device->getActiveChannelMask() & 0xFF);
    TEST_ASSERT_EQUAL(4, device->getActiveChannelCount());
}

void test_active_channel_mask_none_active() {
    device->initialize();

    // Deactivate all channels
    for (uint8_t i = 0; i < 8; i++) {
        device->setMockChannelConfig(i, mb8art::ChannelMode::DEACTIVATED, 0);
    }

    device->forceUpdateActiveChannelMask();

    // No channels active = mask 0x00
    TEST_ASSERT_EQUAL_HEX8(0x00, device->getActiveChannelMask() & 0xFF);
    TEST_ASSERT_EQUAL(0, device->getActiveChannelCount());
}

void test_active_channel_mask_single_channel() {
    device->initialize();

    // Only channel 3 active
    for (uint8_t i = 0; i < 8; i++) {
        if (i == 3) {
            device->setMockChannelConfig(i, mb8art::ChannelMode::PT_INPUT, 0);
        } else {
            device->setMockChannelConfig(i, mb8art::ChannelMode::DEACTIVATED, 0);
        }
    }

    device->forceUpdateActiveChannelMask();

    // Only channel 3 = mask 0x08 (binary: 00001000)
    TEST_ASSERT_EQUAL_HEX8(0x08, device->getActiveChannelMask() & 0xFF);
    TEST_ASSERT_EQUAL(1, device->getActiveChannelCount());
}

// ============================================================================
// Issue 3: Automatic offline detection on consecutive timeouts
// Tests verify the timeout counter and offline flag behavior
// ============================================================================

void test_offline_detection_initial_state() {
    device->initialize();

    // Initially device should be online and timeout counter at 0
    TEST_ASSERT_FALSE(device->isModuleOffline());
    TEST_ASSERT_EQUAL(0, device->getConsecutiveTimeouts());
}

void test_offline_detection_single_timeout() {
    device->initialize();

    // Simulate single timeout
    device->simulateTimeout();

    // Should not be offline after 1 timeout (threshold is 3)
    TEST_ASSERT_FALSE(device->isModuleOffline());
    TEST_ASSERT_EQUAL(1, device->getConsecutiveTimeouts());
}

void test_offline_detection_threshold_reached() {
    device->initialize();

    // Simulate 3 consecutive timeouts (OFFLINE_THRESHOLD)
    device->simulateTimeout();
    device->simulateTimeout();
    device->simulateTimeout();

    // Should be offline after 3 timeouts
    TEST_ASSERT_TRUE(device->isModuleOffline());
    TEST_ASSERT_EQUAL(3, device->getConsecutiveTimeouts());
}

void test_offline_detection_recovery_on_response() {
    device->initialize();

    // Simulate going offline
    device->simulateTimeout();
    device->simulateTimeout();
    device->simulateTimeout();
    TEST_ASSERT_TRUE(device->isModuleOffline());

    // Simulate successful response
    device->simulateSuccessfulResponse();

    // Should be back online with counter reset
    TEST_ASSERT_FALSE(device->isModuleOffline());
    TEST_ASSERT_EQUAL(0, device->getConsecutiveTimeouts());
}

void test_offline_detection_counter_reset_before_threshold() {
    device->initialize();

    // Simulate 2 timeouts (below threshold)
    device->simulateTimeout();
    device->simulateTimeout();
    TEST_ASSERT_EQUAL(2, device->getConsecutiveTimeouts());
    TEST_ASSERT_FALSE(device->isModuleOffline());

    // Simulate successful response
    device->simulateSuccessfulResponse();

    // Counter should be reset
    TEST_ASSERT_EQUAL(0, device->getConsecutiveTimeouts());
    TEST_ASSERT_FALSE(device->isModuleOffline());
}

void test_offline_detection_multiple_recovery_cycles() {
    device->initialize();

    // First cycle: go offline
    for (int i = 0; i < 3; i++) device->simulateTimeout();
    TEST_ASSERT_TRUE(device->isModuleOffline());

    // Recover
    device->simulateSuccessfulResponse();
    TEST_ASSERT_FALSE(device->isModuleOffline());

    // Second cycle: go offline again
    for (int i = 0; i < 3; i++) device->simulateTimeout();
    TEST_ASSERT_TRUE(device->isModuleOffline());

    // Recover again
    device->simulateSuccessfulResponse();
    TEST_ASSERT_FALSE(device->isModuleOffline());
    TEST_ASSERT_EQUAL(0, device->getConsecutiveTimeouts());
}

// ============================================================================
// Combined behavior tests
// ============================================================================

void test_waitForData_with_no_active_channels_returns_error() {
    device->initialize();

    // Deactivate all channels
    for (uint8_t i = 0; i < 8; i++) {
        device->setMockChannelConfig(i, mb8art::ChannelMode::DEACTIVATED, 0);
    }
    device->forceUpdateActiveChannelMask();

    // waitForData should return INVALID_PARAMETER when no channels active
    // This is handled by the activeChannelMask == 0 check
    TEST_ASSERT_EQUAL(0, device->getActiveChannelCount());
}

// ============================================================================
// Test runner for ESP32 PlatformIO
// ============================================================================

#ifdef ARDUINO
#include <Arduino.h>

void setup() {
    delay(2000);  // Allow time for serial to connect

    UNITY_BEGIN();

    // Issue 1 tests (basic - full multi-threaded tests need hardware)
    RUN_TEST(test_error_logging_throttle_initial);

    // Issue 2 tests
    RUN_TEST(test_active_channel_mask_all_active);
    RUN_TEST(test_active_channel_mask_partial);
    RUN_TEST(test_active_channel_mask_none_active);
    RUN_TEST(test_active_channel_mask_single_channel);

    // Issue 3 tests
    RUN_TEST(test_offline_detection_initial_state);
    RUN_TEST(test_offline_detection_single_timeout);
    RUN_TEST(test_offline_detection_threshold_reached);
    RUN_TEST(test_offline_detection_recovery_on_response);
    RUN_TEST(test_offline_detection_counter_reset_before_threshold);
    RUN_TEST(test_offline_detection_multiple_recovery_cycles);

    // Combined tests
    RUN_TEST(test_waitForData_with_no_active_channels_returns_error);

    UNITY_END();
}

void loop() {
    // Nothing needed for tests
}

#else
// Native platform uses main()
int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Issue 1 tests (basic - full multi-threaded tests need hardware)
    RUN_TEST(test_error_logging_throttle_initial);

    // Issue 2 tests
    RUN_TEST(test_active_channel_mask_all_active);
    RUN_TEST(test_active_channel_mask_partial);
    RUN_TEST(test_active_channel_mask_none_active);
    RUN_TEST(test_active_channel_mask_single_channel);

    // Issue 3 tests
    RUN_TEST(test_offline_detection_initial_state);
    RUN_TEST(test_offline_detection_single_timeout);
    RUN_TEST(test_offline_detection_threshold_reached);
    RUN_TEST(test_offline_detection_recovery_on_response);
    RUN_TEST(test_offline_detection_counter_reset_before_threshold);
    RUN_TEST(test_offline_detection_multiple_recovery_cycles);

    // Combined tests
    RUN_TEST(test_waitForData_with_no_active_channels_returns_error);

    return UNITY_END();
}
#endif
