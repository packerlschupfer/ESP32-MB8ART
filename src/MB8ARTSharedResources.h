#ifndef MB8ART_SHARED_RESOURCES_H
#define MB8ART_SHARED_RESOURCES_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

// Forward declaration
class MB8ART;

/**
 * MB8ARTSharedResources provides thread-safe access to shared resources
 * using the Shared Resource Protocol (SRP) pattern.
 * 
 * This class encapsulates all global variables and provides synchronized
 * access methods to prevent race conditions in multi-threaded environments.
 */
class MB8ARTSharedResources {
public:
    // Singleton access
    static MB8ARTSharedResources& getInstance();
    
    // Delete copy constructor and assignment operator
    MB8ARTSharedResources(const MB8ARTSharedResources&) = delete;
    MB8ARTSharedResources& operator=(const MB8ARTSharedResources&) = delete;
    
    // Note: Logger functionality has been removed - use Logger::getInstance() directly
    
    // MB8ART instance access (thread-safe)
    static MB8ART* getMB8ARTInstance();
    static void setMB8ARTInstance(MB8ART* instance);
    
    // Event bits access (thread-safe)
    static EventBits_t getSensorAllUpdateBits();
    static void setSensorAllUpdateBits(EventBits_t bits);
    static EventBits_t getSensorAllErrorBits();
    static void setSensorAllErrorBits(EventBits_t bits);
    
    // Event group operations (thread-safe wrappers)
    static EventBits_t eventGroupSetBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToSet);
    static EventBits_t eventGroupClearBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToClear);
    static EventBits_t eventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                         EventBits_t uxBitsToWaitFor,
                                         BaseType_t xClearOnExit,
                                         BaseType_t xWaitForAllBits,
                                         TickType_t xTicksToWait);
    static EventBits_t eventGroupGetBits(EventGroupHandle_t xEventGroup);
    
private:
    MB8ARTSharedResources();
    ~MB8ARTSharedResources();
    
    // Static instance
    static MB8ARTSharedResources* instance;
    
    // Mutex for thread-safe access
    static SemaphoreHandle_t resourceMutex;
    
    // Shared resources
    static MB8ART* mb8artInstance;
    static EventBits_t sensorAllUpdateBits;
    static EventBits_t sensorAllErrorBits;
    
    // Mutex timeout
    static constexpr TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(1000);
};

// Convenience macros for SRP access
#define MB8ART_SRP MB8ARTSharedResources::getInstance()
#define MB8ART_SRP_MB8ART MB8ARTSharedResources::getMB8ARTInstance()

// Event group operation macros
#define MB8ART_SRP_EVENT_GROUP_SET_BITS(xEventGroup, uxBitsToSet) \
    MB8ARTSharedResources::eventGroupSetBits(xEventGroup, uxBitsToSet)
    
#define MB8ART_SRP_EVENT_GROUP_CLEAR_BITS(xEventGroup, uxBitsToClear) \
    MB8ARTSharedResources::eventGroupClearBits(xEventGroup, uxBitsToClear)
    
#define MB8ART_SRP_EVENT_GROUP_WAIT_BITS(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait) \
    MB8ARTSharedResources::eventGroupWaitBits(xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait)
    
#define MB8ART_SRP_EVENT_GROUP_GET_BITS(xEventGroup) \
    MB8ARTSharedResources::eventGroupGetBits(xEventGroup)

#endif // MB8ART_SHARED_RESOURCES_H