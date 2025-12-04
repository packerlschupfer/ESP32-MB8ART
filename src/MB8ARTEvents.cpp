/**
 * @file MB8ARTEvents.cpp
 * @brief Event group and notification handling
 *
 * This file contains event group and notification handling for the MB8ART library.
 * Uses RYN4 interleaved bit pattern: U0 E0 U1 E1 ... U7 E7
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

    // Use constexpr arrays for interleaved bit positions
    uint32_t updateBit = mb8art::SENSOR_UPDATE_BITS[sensorIndex];
    uint32_t errorBit = mb8art::SENSOR_ERROR_BITS[sensorIndex];

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

    // Use critical section to prevent race conditions
    taskENTER_CRITICAL(&clearDataMutex);

    // Build interleaved mask from active channel mask
    // activeChannelMask uses simple bits (0-7), need to convert to interleaved format
    uint32_t interleavedMask = 0;
    for (uint8_t i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
        if (activeChannelMask & (1 << i)) {
            interleavedMask |= mb8art::SENSOR_UPDATE_BITS[i] | mb8art::SENSOR_ERROR_BITS[i];
        }
    }

    // Clear all active channel bits from sensor event group
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xSensorEventGroup, interleavedMask);

    // Clear task communication bits
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xTaskEventGroup, DATA_READY_BIT | DATA_ERROR_BIT);

    taskEXIT_CRITICAL(&clearDataMutex);

    LOG_MB8ART_DEBUG_NL("Cleared event bits for active channels (mask: 0x%04X)", interleavedMask);
}


