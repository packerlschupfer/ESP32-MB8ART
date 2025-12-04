// include/init/SystemInitializer.h
// MB8ART Full Example - System Initialization Manager
#pragma once

#include "config/ProjectConfig.h"

// Forward declarations
class MB8ART;
class TaskManager;

/**
 * @brief Manages system initialization with proper error handling
 *
 * Initialization stages:
 * 1. Logging - Serial and Logger setup
 * 2. Hardware - GPIO, RS485
 * 3. Network - Ethernet, OTA
 * 4. Modbus - MB8ART device
 * 5. Tasks - FreeRTOS tasks
 */
class SystemInitializer {
public:
    enum class InitStage {
        NONE = 0,
        LOGGING,
        HARDWARE,
        NETWORK,
        MODBUS,
        TASKS,
        COMPLETE
    };

    SystemInitializer();
    ~SystemInitializer();

    /**
     * @brief Initialize the entire system
     * @return Result indicating success or failure
     */
    Result<void> initialize();

    /**
     * @brief Get current initialization stage
     */
    InitStage getCurrentStage() const { return currentStage_; }

    /**
     * @brief Check if system is fully initialized
     */
    bool isFullyInitialized() const { return currentStage_ == InitStage::COMPLETE; }

    /**
     * @brief Get MB8ART device instance
     */
    MB8ART* getMB8ART() const { return mb8art_; }

    /**
     * @brief Get network connection status
     */
    bool isNetworkConnected() const { return networkConnected_; }

    /**
     * @brief Cleanup all resources
     */
    void cleanup();

private:
    // Initialization stages
    Result<void> initializeLogging();
    Result<void> initializeHardware();
    Result<void> initializeNetwork();
    Result<void> initializeModbus();
    Result<void> initializeTasks();

    // Cleanup stages (reverse order)
    void cleanupTasks();
    void cleanupModbus();
    void cleanupNetwork();
    void cleanupHardware();

    // State
    InitStage currentStage_;
    bool networkConnected_;

    // Device pointers
    MB8ART* mb8art_;
};

// Global instance
extern SystemInitializer* gSystemInitializer;
