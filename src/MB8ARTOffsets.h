#ifndef MB8ART_OFFSETS_H
#define MB8ART_OFFSETS_H

#include <stdint.h>

/**
 * @file MB8ARTOffsets.h
 * @brief Per-channel temperature offset rules
 *
 * Kept free of FreeRTOS and Modbus dependencies so it can be unit tested natively.
 * MB8ART uses these functions directly; they are the single source of the rules.
 *
 * Validation and applicability are deliberately split:
 *  - At SET time only the channel index and the offset magnitude are checked.
 *    The channel mode is not known until the first config read from the device
 *    (all modes start as DEACTIVATED), so a mode check here would drop offsets
 *    that an application restores at boot.
 *  - At APPLY time, per reading, the channel mode must be a temperature mode.
 *    Current and voltage channels (e.g. a 4-20mA pressure transducer) are never
 *    offset, whatever was stored for them.
 */

namespace mb8art {

// Largest accepted offset magnitude, in tenths of a degree (50.0 C)
static constexpr int16_t MAX_CHANNEL_OFFSET_TENTHS = 500;

// Valid window for a corrected reading, in tenths (matches the range check in
// updateSensorReading(); see HARDWARE.md)
static constexpr int16_t MIN_CORRECTED_TENTHS = -2000;     // -200.0 C
static constexpr int16_t MAX_CORRECTED_TENTHS_PT = 8500;   //  850.0 C
static constexpr int16_t MAX_CORRECTED_TENTHS_TC = 18000;  // 1800.0 C (type B)

static constexpr uint8_t OFFSET_CHANNEL_COUNT = 8;

// Raw ChannelMode values (mirrors mb8art::ChannelMode in MB8ART.h)
static constexpr uint16_t OFFSET_MODE_THERMOCOUPLE = 0x01;
static constexpr uint16_t OFFSET_MODE_PT_INPUT = 0x02;

/**
 * @brief Set-time validation: channel index and offset range only
 */
inline bool isValidChannelOffset(uint8_t channel, int16_t offsetTenths) {
    return channel < OFFSET_CHANNEL_COUNT &&
           offsetTenths >= -MAX_CHANNEL_OFFSET_TENTHS &&
           offsetTenths <= MAX_CHANNEL_OFFSET_TENTHS;
}

/**
 * @brief Apply-time gate: only temperature channels are offset
 */
inline bool isTemperatureMode(uint16_t mode) {
    return mode == OFFSET_MODE_PT_INPUT || mode == OFFSET_MODE_THERMOCOUPLE;
}

/**
 * @brief Apply an offset to a reading in tenths
 *
 * Returns the reading unchanged for non-temperature modes. The sum is computed in
 * int32 and saturated to the mode's valid window, so it cannot overflow.
 */
inline int16_t applyChannelOffset(int16_t valueInTenths, int16_t offsetTenths, uint16_t mode) {
    if (offsetTenths == 0 || !isTemperatureMode(mode)) {
        return valueInTenths;
    }
    const int32_t maxCorrected = (mode == OFFSET_MODE_THERMOCOUPLE) ? MAX_CORRECTED_TENTHS_TC
                                                                    : MAX_CORRECTED_TENTHS_PT;
    int32_t corrected = static_cast<int32_t>(valueInTenths) + offsetTenths;
    if (corrected < MIN_CORRECTED_TENTHS) corrected = MIN_CORRECTED_TENTHS;
    if (corrected > maxCorrected) corrected = maxCorrected;
    return static_cast<int16_t>(corrected);
}

}  // namespace mb8art

#endif  // MB8ART_OFFSETS_H
