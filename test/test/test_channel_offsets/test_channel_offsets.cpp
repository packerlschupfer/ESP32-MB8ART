/**
 * @file test_channel_offsets.cpp
 * @brief Native tests for the per-channel temperature offset rules (MB8ARTOffsets.h)
 *
 * Run: cd test && pio test -e native -f test_channel_offsets
 */

#include <unity.h>
#include <array>
#include "MB8ARTOffsets.h"

using namespace mb8art;

static constexpr uint16_t MODE_DEACTIVATED = 0x00;
static constexpr uint16_t MODE_THERMOCOUPLE = 0x01;
static constexpr uint16_t MODE_PT_INPUT = 0x02;
static constexpr uint16_t MODE_VOLTAGE = 0x03;
static constexpr uint16_t MODE_CURRENT = 0x04;

void setUp() {}
void tearDown() {}

// Default-zero offsets must reproduce the uncorrected value exactly, for every mode
void test_zero_offset_is_identity() {
    const uint16_t modes[] = {MODE_DEACTIVATED, MODE_THERMOCOUPLE, MODE_PT_INPUT, MODE_VOLTAGE, MODE_CURRENT};
    const int16_t values[] = {-2000, -1, 0, 1, 234, 8500, 1075};
    for (uint16_t mode : modes) {
        for (int16_t v : values) {
            TEST_ASSERT_EQUAL_INT16(v, applyChannelOffset(v, 0, mode));
        }
    }
}

void test_pt_channel_is_offset() {
    TEST_ASSERT_EQUAL_INT16(211, applyChannelOffset(234, -23, MODE_PT_INPUT));  // 23.4 - 2.3
    TEST_ASSERT_EQUAL_INT16(239, applyChannelOffset(234, 5, MODE_PT_INPUT));    // 23.4 + 0.5
    TEST_ASSERT_EQUAL_INT16(-28, applyChannelOffset(-5, -23, MODE_PT_INPUT));   // -0.5 - 2.3
}

void test_thermocouple_channel_is_offset() {
    TEST_ASSERT_EQUAL_INT16(1210, applyChannelOffset(1200, 10, MODE_THERMOCOUPLE));
}

// A 4-20mA pressure channel (mode 0x04, value in hundredths of mA) must never move
void test_current_channel_is_never_offset() {
    TEST_ASSERT_EQUAL_INT16(1075, applyChannelOffset(1075, 50, MODE_CURRENT));
    TEST_ASSERT_EQUAL_INT16(1075, applyChannelOffset(1075, -500, MODE_CURRENT));
}

void test_voltage_and_deactivated_channels_are_never_offset() {
    TEST_ASSERT_EQUAL_INT16(300, applyChannelOffset(300, 50, MODE_VOLTAGE));
    TEST_ASSERT_EQUAL_INT16(300, applyChannelOffset(300, 50, MODE_DEACTIVATED));
}

void test_set_validation_channel_index() {
    TEST_ASSERT_TRUE(isValidChannelOffset(0, 10));
    TEST_ASSERT_TRUE(isValidChannelOffset(7, 10));
    TEST_ASSERT_FALSE(isValidChannelOffset(8, 10));
    TEST_ASSERT_FALSE(isValidChannelOffset(255, 0));
}

void test_set_validation_offset_range() {
    TEST_ASSERT_TRUE(isValidChannelOffset(3, MAX_CHANNEL_OFFSET_TENTHS));
    TEST_ASSERT_TRUE(isValidChannelOffset(3, -MAX_CHANNEL_OFFSET_TENTHS));
    TEST_ASSERT_FALSE(isValidChannelOffset(3, MAX_CHANNEL_OFFSET_TENTHS + 1));
    TEST_ASSERT_FALSE(isValidChannelOffset(3, -MAX_CHANNEL_OFFSET_TENTHS - 1));
    TEST_ASSERT_FALSE(isValidChannelOffset(3, INT16_MAX));
    TEST_ASSERT_FALSE(isValidChannelOffset(3, INT16_MIN));
}

// Set-time validation must not depend on the mode: every mode starts as DEACTIVATED
// until the first config read, and offsets restored at boot must not be dropped
void test_set_validation_ignores_mode() {
    for (uint8_t ch = 0; ch < OFFSET_CHANNEL_COUNT; ch++) {
        TEST_ASSERT_TRUE(isValidChannelOffset(ch, -23));
    }
}

// Ordering: an offset stored while the mode is still unknown (0) takes effect on the
// first reading after the config read reports PT_INPUT
void test_offset_stored_before_config_read_applies_after() {
    std::array<int16_t, 8> stored = {{0, 0, 0, -23, 0, 0, 0, 0}};
    uint16_t mode = MODE_DEACTIVATED;  // before the config read
    TEST_ASSERT_TRUE(isValidChannelOffset(3, stored[3]));
    TEST_ASSERT_EQUAL_INT16(150, applyChannelOffset(150, stored[3], mode));

    mode = MODE_PT_INPUT;  // config read landed
    TEST_ASSERT_EQUAL_INT16(127, applyChannelOffset(150, stored[3], mode));
}

// A channel changed to current mode at runtime stops being offset immediately
void test_mode_change_to_current_stops_offset() {
    TEST_ASSERT_EQUAL_INT16(127, applyChannelOffset(150, -23, MODE_PT_INPUT));
    TEST_ASSERT_EQUAL_INT16(150, applyChannelOffset(150, -23, MODE_CURRENT));
}

void test_saturates_to_valid_window() {
    TEST_ASSERT_EQUAL_INT16(MAX_CORRECTED_TENTHS_PT, applyChannelOffset(8480, 50, MODE_PT_INPUT));
    TEST_ASSERT_EQUAL_INT16(MIN_CORRECTED_TENTHS, applyChannelOffset(-1990, -50, MODE_PT_INPUT));
    TEST_ASSERT_EQUAL_INT16(MAX_CORRECTED_TENTHS_PT, applyChannelOffset(INT16_MAX, 500, MODE_PT_INPUT));
    TEST_ASSERT_EQUAL_INT16(MIN_CORRECTED_TENTHS, applyChannelOffset(INT16_MIN, -500, MODE_PT_INPUT));
}

// Thermocouples are valid up to 1800.0 C, so they must not be clamped at the PT limit
void test_thermocouple_uses_wider_window() {
    TEST_ASSERT_EQUAL_INT16(12010, applyChannelOffset(12000, 10, MODE_THERMOCOUPLE));
    TEST_ASSERT_EQUAL_INT16(MAX_CORRECTED_TENTHS_TC, applyChannelOffset(17980, 50, MODE_THERMOCOUPLE));
    TEST_ASSERT_EQUAL_INT16(MIN_CORRECTED_TENTHS, applyChannelOffset(-1990, -50, MODE_THERMOCOUPLE));
}

// HIGH_RES: the library rounds hundredths to tenths first, then applies the offset
void test_offset_applies_after_high_res_rounding() {
    int16_t hundredths = 2346;                    // 23.46 C
    int16_t tenths = (hundredths + 5) / 10;       // 235, as in updateSensorReading()
    TEST_ASSERT_EQUAL_INT16(212, applyChannelOffset(tenths, -23, MODE_PT_INPUT));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_offset_is_identity);
    RUN_TEST(test_pt_channel_is_offset);
    RUN_TEST(test_thermocouple_channel_is_offset);
    RUN_TEST(test_current_channel_is_never_offset);
    RUN_TEST(test_voltage_and_deactivated_channels_are_never_offset);
    RUN_TEST(test_set_validation_channel_index);
    RUN_TEST(test_set_validation_offset_range);
    RUN_TEST(test_set_validation_ignores_mode);
    RUN_TEST(test_offset_stored_before_config_read_applies_after);
    RUN_TEST(test_mode_change_to_current_stops_offset);
    RUN_TEST(test_saturates_to_valid_window);
    RUN_TEST(test_thermocouple_uses_wider_window);
    RUN_TEST(test_offset_applies_after_high_res_rounding);
    return UNITY_END();
}
