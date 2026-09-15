/*
 * MB8ARTEvents.cpp - part of the ESP32-MB8ART library
 *
 * Copyright (C) 2025-2026 packerlschupfer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file MB8ARTEvents.cpp
 * @brief Event group and notification handling
 *
 * This file contains event group and notification handling for the MB8ART library.
 * Default bit pattern (no setHardwareConfig()): RYN4 interleaved U0 E0 U1 E1 ... U7 E7.
 * With a hardware config, each channel uses the bits from its config entry.
 */

#include "MB8ART.h"
#include <MutexGuard.h>


using namespace mb8art;

void MB8ART::notifyDataReceiver() {
    if (dataReceiverTask != nullptr) {
        xTaskNotifyGive(dataReceiverTask);
        LOG_MB8ART_DEBUG_NL("Notified data receiver task");
    }
}

EventBits_t MB8ART::updateBitFor(uint8_t channel) const {
    if (channel >= DEFAULT_NUMBER_OF_SENSORS) {
        return 0;
    }
    return hardwareConfig ? hardwareConfig[channel].updateEventBit : mb8art::SENSOR_UPDATE_BITS[channel];
}

EventBits_t MB8ART::errorBitFor(uint8_t channel) const {
    if (channel >= DEFAULT_NUMBER_OF_SENSORS) {
        return 0;
    }
    return hardwareConfig ? hardwareConfig[channel].errorEventBit : mb8art::SENSOR_ERROR_BITS[channel];
}

EventBits_t MB8ART::allUpdateBits() const {
    EventBits_t bits = 0;
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        bits |= updateBitFor(i);
    }
    return bits;
}

EventBits_t MB8ART::allErrorBits() const {
    EventBits_t bits = 0;
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        bits |= errorBitFor(i);
    }
    return bits;
}



void MB8ART::updateEventBits(EventBits_t updateBitsToSet,
                            EventBits_t errorBitsToSet,
                            EventBits_t errorBitsToClear) {
    // Single event group with interleaved bits - no shifting needed!
    // Bits are already in correct positions from SENSOR_UPDATE_BITS/SENSOR_ERROR_BITS arrays

    // Handle error bits - clear first, then set
    if (errorBitsToClear) {
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, errorBitsToClear);
        LOG_MB8ART_DEBUG_NL("Clearing error bits: 0x%04X", errorBitsToClear);
    }

    // Handle update bits
    if (updateBitsToSet) {
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xSensorEventGroup, updateBitsToSet);
        LOG_MB8ART_DEBUG_NL("Setting update bits: 0x%04X", updateBitsToSet);
    }

    if (errorBitsToSet) {
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xSensorEventGroup, errorBitsToSet);
        LOG_MB8ART_DEBUG_NL("Setting error bits: 0x%04X", errorBitsToSet);
    }

    // Notify the data receiver task about errors
    if (errorBitsToSet && dataReceiverTask) {
        xTaskNotify(dataReceiverTask, DATA_ERROR_BIT, eSetBits);
        LOG_MB8ART_DEBUG_NL("Notified data receiver task about errors");
    }

    LOG_MB8ART_DEBUG_NL("Event bits updated - update: 0x%04X, error set: 0x%04X, error clear: 0x%04X",
                       updateBitsToSet, errorBitsToSet, errorBitsToClear);
}




void MB8ART::clearUpdateEventBits(uint32_t bitsToClear) {
    if (xSensorEventGroup) {
        // Bits are already in interleaved positions
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, bitsToClear);
        LOG_MB8ART_DEBUG_NL("Cleared update bits: 0x%04X", bitsToClear);
    }
}




void MB8ART::clearErrorEventBits(uint32_t bitsToClear) {
    if (xSensorEventGroup) {
        // Bits are already in interleaved positions
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, bitsToClear);
        LOG_MB8ART_DEBUG_NL("Cleared error bits: 0x%04X", bitsToClear);
    }
}

void MB8ART::setUpdateEventBits(uint32_t bitsToSet) {
    if (xSensorEventGroup) {
        // Bits are already in interleaved positions
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xSensorEventGroup, bitsToSet);
        LOG_MB8ART_DEBUG_NL("Set update bits: 0x%04X", bitsToSet);
    }
}

void MB8ART::setErrorEventBits(uint32_t bitsToSet) {
    if (xSensorEventGroup) {
        // Bits are already in interleaved positions
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xSensorEventGroup, bitsToSet);
        LOG_MB8ART_DEBUG_NL("Set error bits: 0x%04X", bitsToSet);
    }
}





// checkAllInitBitsSet remains in MB8ART.cpp (initialization logic)




void MB8ART::updateSensorEventBits(uint8_t sensorIndex, bool isValid, bool hasError) {
    if (sensorIndex >= DEFAULT_NUMBER_OF_SENSORS) {
        return;
    }

    uint32_t updateBit = updateBitFor(sensorIndex);
    uint32_t errorBit = errorBitFor(sensorIndex);

    if (isValid) {
        setUpdateEventBits(updateBit);
        if (!hasError) {
            clearErrorEventBits(errorBit);
        }
    } else {
        clearUpdateEventBits(updateBit);
    }

    if (hasError) {
        setErrorEventBits(errorBit);
    }
}




// Helper method for thread-safe event bit clearing
void MB8ART::clearDataEventBits() {
    // ESP32 requires a spinlock for critical sections
    static portMUX_TYPE clearDataMutex = portMUX_INITIALIZER_UNLOCKED;

    // Build the event bit mask of the active channels (activeChannelMask uses bits 0-7)
    uint32_t interleavedMask = 0;
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (activeChannelMask & (1 << i)) {
            interleavedMask |= updateBitFor(i) | errorBitFor(i);
        }
    }

    // Use critical section to prevent race conditions
    taskENTER_CRITICAL(&clearDataMutex);

    // Clear all active channel bits from sensor event group
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, interleavedMask);

    // Clear task communication bits
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xTaskEventGroup, DATA_READY_BIT | DATA_ERROR_BIT);

    taskEXIT_CRITICAL(&clearDataMutex);

    LOG_MB8ART_DEBUG_NL("Cleared event bits for active channels (mask: 0x%04X)", interleavedMask);
}


