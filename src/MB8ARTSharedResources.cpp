#include "MB8ARTSharedResources.h"
#include "MB8ART.h"
#include <MutexGuard.h>
#include <esp_log.h>
#include "MB8ARTLoggingMacros.h"

// Static member initialization
// Define MB8ART's static member variables
TickType_t MB8ART::lastGlobalDataUpdate = 0;
uint32_t MB8ART::expectedUpdateIntervalMs = 0;

// Static member initialization
MB8ARTSharedResources* MB8ARTSharedResources::instance = nullptr;
SemaphoreHandle_t MB8ARTSharedResources::resourceMutex = nullptr;
MB8ART* MB8ARTSharedResources::mb8artInstance = nullptr;
EventBits_t MB8ARTSharedResources::sensorAllUpdateBits = 0xFF;  // All 8 sensors
EventBits_t MB8ARTSharedResources::sensorAllErrorBits = 0xFF;   // All 8 sensors

// Constructor
MB8ARTSharedResources::MB8ARTSharedResources() {
    // Create mutex for thread-safe access
    if (resourceMutex == nullptr) {
        resourceMutex = xSemaphoreCreateMutex();
        configASSERT(resourceMutex != nullptr);
    }
    
    LOG_MB8ART_DEBUG_NL("MB8ARTSharedResources: Constructor complete");
}

// Destructor
MB8ARTSharedResources::~MB8ARTSharedResources() {
    if (resourceMutex != nullptr) {
        vSemaphoreDelete(resourceMutex);
        resourceMutex = nullptr;
    }
}

// Singleton access
MB8ARTSharedResources& MB8ARTSharedResources::getInstance() {
    if (instance == nullptr) {
        instance = new MB8ARTSharedResources();
    }
    return *instance;
}

// MB8ART instance access (thread-safe)
MB8ART* MB8ARTSharedResources::getMB8ARTInstance() {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        // Log error but return current value anyway
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for getMB8ARTInstance");
    }
    return mb8artInstance;
}

void MB8ARTSharedResources::setMB8ARTInstance(MB8ART* instance) {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for setMB8ARTInstance");
        return;
    }
    mb8artInstance = instance;
}

// Event bits access (thread-safe)
EventBits_t MB8ARTSharedResources::getSensorAllUpdateBits() {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for getSensorAllUpdateBits");
    }
    return sensorAllUpdateBits;
}

void MB8ARTSharedResources::setSensorAllUpdateBits(EventBits_t bits) {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for setSensorAllUpdateBits");
        return;
    }
    sensorAllUpdateBits = bits;
}

EventBits_t MB8ARTSharedResources::getSensorAllErrorBits() {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for getSensorAllErrorBits");
    }
    return sensorAllErrorBits;
}

void MB8ARTSharedResources::setSensorAllErrorBits(EventBits_t bits) {
    MutexGuard guard(getInstance().resourceMutex, MUTEX_TIMEOUT);
    if (!guard.hasLock()) {
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: Failed to acquire mutex for setSensorAllErrorBits");
        return;
    }
    sensorAllErrorBits = bits;
}

// Event group operations (thread-safe wrappers)
EventBits_t MB8ARTSharedResources::eventGroupSetBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToSet) {
    // xEventGroupSetBits is already thread-safe in FreeRTOS
    // This wrapper provides a consistent interface and logging capability
    if (xEventGroup == nullptr) {
        // Log error for null event group
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: eventGroupSetBits called with null event group");
        return 0;
    }
    return xEventGroupSetBits(xEventGroup, uxBitsToSet);
}

EventBits_t MB8ARTSharedResources::eventGroupClearBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToClear) {
    // xEventGroupClearBits is already thread-safe in FreeRTOS
    if (xEventGroup == nullptr) {
        // Log error for null event group
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: eventGroupClearBits called with null event group");
        return 0;
    }
    return xEventGroupClearBits(xEventGroup, uxBitsToClear);
}

EventBits_t MB8ARTSharedResources::eventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                                      EventBits_t uxBitsToWaitFor,
                                                      BaseType_t xClearOnExit,
                                                      BaseType_t xWaitForAllBits,
                                                      TickType_t xTicksToWait) {
    // xEventGroupWaitBits is already thread-safe in FreeRTOS
    if (xEventGroup == nullptr) {
        // Log error for null event group
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: eventGroupWaitBits called with null event group");
        return 0;
    }
    return xEventGroupWaitBits(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait);
}

EventBits_t MB8ARTSharedResources::eventGroupGetBits(EventGroupHandle_t xEventGroup) {
    // xEventGroupGetBits is already thread-safe in FreeRTOS
    if (xEventGroup == nullptr) {
        // Log error for null event group
        LOG_MB8ART_ERROR_NL("MB8ARTSharedResources: eventGroupGetBits called with null event group");
        return 0;
    }
    return xEventGroupGetBits(xEventGroup);
}