/**
 * @file MB8ARTEvents.cpp
 * @brief Event group and notification handling
 * 
 * This file contains event group and notification handling for the MB8ART library.
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
    // With three event groups, no bit shifting needed!
    
    // Handle update bits
    if (updateBitsToSet) {
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xUpdateEventGroup, updateBitsToSet);
        LOG_MB8ART_DEBUG_NL("Setting update bits: 0x%02X", updateBitsToSet);
    }
    
    // Handle error bits - clear first, then set
    if (errorBitsToClear) {
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xErrorEventGroup, errorBitsToClear);
        LOG_MB8ART_DEBUG_NL("Clearing error bits: 0x%02X", errorBitsToClear);
    }
    if (errorBitsToSet) {
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xErrorEventGroup, errorBitsToSet);
        LOG_MB8ART_DEBUG_NL("Setting error bits: 0x%02X", errorBitsToSet);
    }
    
    // Notify the data receiver task about errors
    if (errorBitsToSet && dataReceiverTask) {
        xTaskNotify(dataReceiverTask, DATA_ERROR_BIT, eSetBits);
        LOG_MB8ART_DEBUG_NL("Notified data receiver task about errors");
    }
    
    LOG_MB8ART_DEBUG_NL("Event bits updated - update: 0x%02X, error set: 0x%02X, error clear: 0x%02X", 
                       updateBitsToSet, errorBitsToSet, errorBitsToClear);
}




void MB8ART::clearUpdateEventBits(uint32_t bitsToClear) {
    if (xUpdateEventGroup) {
        // Direct bit access - no shifting needed!
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xUpdateEventGroup, bitsToClear);
        LOG_MB8ART_DEBUG_NL("Cleared update bits: 0x%X", bitsToClear);
    }
}




void MB8ART::clearErrorEventBits(uint32_t bitsToClear) {
    if (xErrorEventGroup) {
        // Direct bit access - no shifting needed!
        MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xErrorEventGroup, bitsToClear);
        LOG_MB8ART_DEBUG_NL("Cleared error bits: 0x%X", bitsToClear);
    }
}

void MB8ART::setUpdateEventBits(uint32_t bitsToSet) {
    if (xUpdateEventGroup) {
        // Direct bit access - no shifting needed!
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xUpdateEventGroup, bitsToSet);
        LOG_MB8ART_DEBUG_NL("Set update bits: 0x%X", bitsToSet);
    }
}

void MB8ART::setErrorEventBits(uint32_t bitsToSet) {
    if (xErrorEventGroup) {
        // Direct bit access - no shifting needed!
        MB8ART_SRP_EVENT_GROUP_SET_BITS(xErrorEventGroup, bitsToSet);
        LOG_MB8ART_DEBUG_NL("Set error bits: 0x%X", bitsToSet);
    }
}





// checkAllInitBitsSet remains in MB8ART.cpp (initialization logic)





void MB8ART::updateSensorEventBits(uint8_t sensorIndex, bool isValid, bool hasError) {
    if (sensorIndex >= DEFAULT_NUMBER_OF_SENSORS) {
        return;
    }
    
    EventBits_t updateBit = mb8art::toUnderlyingType(mb8art::getSensorUpdateBit(sensorIndex));
    EventBits_t errorBit = mb8art::toUnderlyingType(mb8art::getSensorErrorBit(sensorIndex));
    
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
    
    // With three event groups, no bit shifting needed!
    // Clear all active channel bits from both update and error event groups
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xUpdateEventGroup, activeChannelMask);
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xErrorEventGroup, activeChannelMask);
    
    // Clear task communication bits
    MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xTaskEventGroup, DATA_READY_BIT | DATA_ERROR_BIT);
    
    taskEXIT_CRITICAL(&clearDataMutex);
    
    LOG_MB8ART_DEBUG_NL("Cleared event bits for active channels (mask: 0x%06X)", activeChannelMask);
}


