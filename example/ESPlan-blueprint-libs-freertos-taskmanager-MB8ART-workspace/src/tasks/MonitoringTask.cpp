// tasks/MonitoringTask.cpp
#include "tasks/MonitoringTask.h"
#include "tasks/TemperatureTask.h"
#include "MB8ART.h"
#include <ModbusDevice.h>
#include <TaskManager.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#ifdef USE_WIFI
#include <WiFi.h>
#endif

// External references
extern MB8ART* temperatureModule;
extern bool networkConnected;
extern TaskManager taskManager;

// Static member definitions
uint32_t MonitoringTask::cycleCount = 0;

// Static buffer for log messages to avoid stack allocation
static char logBuffer[256];

bool MonitoringTask::init() {
    cycleCount = 0;
    LOG_INFO(TASK_TAG, "Monitoring task initialized");
    return true;
}

bool MonitoringTask::start() {
    if (isRunning()) {
        LOG_WARN(TASK_TAG, "Task already running");
        return true;
    }
    
    // Create the FreeRTOS task with watchdog config
    TaskManager::WatchdogConfig wdtConfig = TaskManager::WatchdogConfig::enabled(
        true, MONITORING_TASK_WATCHDOG_TIMEOUT_MS);
    
    if (!taskManager.startTask(taskFunction, TASK_NAME, STACK_SIZE, 
                              nullptr, TASK_PRIORITY, wdtConfig)) {
        LOG_ERROR(TASK_TAG, "Failed to create task");
        return false;
    }
    
    LOG_INFO(TASK_TAG, "Task started successfully with %d ms watchdog timeout", 
             MONITORING_TASK_WATCHDOG_TIMEOUT_MS);
    return true;
}

void MonitoringTask::stop() {
    TaskHandle_t handle = taskManager.getTaskHandleByName(TASK_NAME);
    if (handle != nullptr) {
        taskManager.stopTask(handle);
        LOG_INFO(TASK_TAG, "Task stopped");
    }
}

bool MonitoringTask::isRunning() {
    return taskManager.getTaskHandleByName(TASK_NAME) != nullptr;
}

TaskHandle_t MonitoringTask::getTaskHandle() {
    return taskManager.getTaskHandleByName(TASK_NAME);
}

void MonitoringTask::taskFunction(void* pvParameters) {
    LOG_INFO(TASK_TAG, "System monitoring task started");
    
    // Wait before starting main loop to ensure task is registered with watchdog
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    LOG_INFO(TASK_TAG, "Monitoring task entering main loop");
    
    // Task loop
    for (;;) {
        // Feed watchdog at the start of each iteration
        taskManager.feedWatchdog();
        
        cycleCount++;
        
        LOG_INFO(TASK_TAG, "=== System Monitor Report #%d ===", cycleCount);
        
        // Log various system aspects
        logSystemHealth();
        
        // Feed watchdog after potentially long operation
        taskManager.feedWatchdog();
        
        logNetworkStatus();
        
        // Feed watchdog
        taskManager.feedWatchdog();
        
        logModuleStatus();
        
        // Feed watchdog
        taskManager.feedWatchdog();
        
        logTemperatureStatistics();
        
        // Feed watchdog
        taskManager.feedWatchdog();
        
        logTaskStatus();
        
        LOG_INFO(TASK_TAG, "=== End of Report ===\n");
        
        // Before any long delays, feed watchdog again
        taskManager.feedWatchdog();
        
        // Handle delay with proper watchdog feeding
        const int totalDelayMs = MONITOR_INTERVAL_MS;
        
        // For delays longer than watchdog timeout, we need to feed it periodically
        // ESP32 watchdog typically has a 5-second timeout, so feed every 2 seconds for safety
        const int maxDelayWithoutFeed = 2000;  // 2 seconds
        
        if (totalDelayMs <= maxDelayWithoutFeed) {
            // Short delay - can do it in one go
            vTaskDelay(pdMS_TO_TICKS(totalDelayMs));
            taskManager.feedWatchdog();
        } else {
            // Long delay - need to break it up and feed watchdog periodically
            int remainingMs = totalDelayMs;
            
            #if defined(LOG_MODE_DEBUG_FULL)
            LOG_DEBUG(TASK_TAG, "Entering delay period: %d ms (feeding watchdog every %d ms)", 
                     totalDelayMs, maxDelayWithoutFeed);
            #endif
            
            while (remainingMs > 0) {
                // Calculate delay for this iteration
                int delayMs = (remainingMs > maxDelayWithoutFeed) ? maxDelayWithoutFeed : remainingMs;
                
                // Delay
                vTaskDelay(pdMS_TO_TICKS(delayMs));
                
                // Update remaining time
                remainingMs -= delayMs;
                
                // Feed watchdog after each chunk
                taskManager.feedWatchdog();
            }
        }
    }
}

void MonitoringTask::logSystemHealth() {
    // Get free heap memory
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t heapSize = ESP.getHeapSize();
    
    // Calculate percentage with integer math to avoid float operations
    uint32_t heapPercent = (freeHeap * 100) / heapSize;
    
    // Get minimum free heap since boot
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    
    // Memory leak detection - improved version from RYN4
    static uint32_t lastFreeHeap = 0;
    static bool firstRun = true;
    static uint32_t stabilizedHeap = 0;     // Heap after system stabilization
    static bool systemStabilized = false;   // Flag to track if system is stable
    static uint32_t stabilizationTime = 0;  // Time when system stabilized
    static uint32_t leakAccumulator = 0;    // Track total leaked memory after stabilization
    static uint32_t monitoringCycles = 0;   // Count monitoring cycles
    static uint32_t consecutiveLeaks = 0;   // Track consecutive memory losses
    
    monitoringCycles++;
    
    if (firstRun) {
        lastFreeHeap = freeHeap;
        firstRun = false;
        LOG_INFO(TASK_TAG, "Memory monitoring initialized - baseline heap: %lu bytes", 
                 (unsigned long)freeHeap);
    } else {
        int32_t heapDelta = (int32_t)freeHeap - (int32_t)lastFreeHeap;
        
        // System stabilization detection
        // Consider system stable after 5 monitoring cycles or 5 minutes
        if (!systemStabilized) {
            if (monitoringCycles >= 5 || millis() > 300000) {  // 5 cycles or 5 minutes
                systemStabilized = true;
                stabilizedHeap = freeHeap;
                stabilizationTime = millis();
                LOG_INFO(TASK_TAG, "System memory stabilized at %lu bytes after %lu ms", 
                         (unsigned long)stabilizedHeap, (unsigned long)stabilizationTime);
            } else {
                // During initialization, just track changes without reporting leaks
                if (heapDelta < -5000) {
                    LOG_DEBUG(TASK_TAG, "Memory allocation during init: %ld bytes", 
                             (long)(-heapDelta));
                }
            }
        } else {
            // After stabilization, monitor for real leaks
            // Only log significant changes to avoid noise
            if (heapDelta < -2000) {  // Lost more than 2KB
                // Check if this is a persistent leak or temporary allocation
                consecutiveLeaks++;
                
                // Only report as leak if we see consistent memory loss
                if (consecutiveLeaks >= 3) {
                    leakAccumulator += (uint32_t)(-heapDelta);
                    
                    // Log for significant single-cycle losses
                    if (-heapDelta > 10000) {  // More than 10KB in one go
                        LOG_WARN(TASK_TAG, "Large memory allocation: %ld bytes", 
                                 (long)(-heapDelta));
                        LOG_INFO(TASK_TAG, "Current free heap: %lu bytes (min: %lu)", 
                                (unsigned long)freeHeap, (unsigned long)minFreeHeap);
                    } else if (leakAccumulator > 20000) {  // Total loss > 20KB
                        LOG_WARN(TASK_TAG, "Potential memory leak: Lost %ld bytes (Total: %lu bytes)", 
                                 (long)(-heapDelta), (unsigned long)leakAccumulator);
                    }
                    
                    // Only report critical if we're actually running low on memory
                    if (freeHeap < 50000 && leakAccumulator > 30000) {
                        LOG_ERROR(TASK_TAG, "CRITICAL: Low memory with potential leak!");
                        LOG_ERROR(TASK_TAG, "Free heap: %lu bytes, Total lost: %lu bytes", 
                                 (unsigned long)freeHeap, (unsigned long)leakAccumulator);
                        
                        // Calculate leak rate only after stabilization
                        uint32_t timeSinceStable = millis() - stabilizationTime;
                        if (timeSinceStable > 60000) {  // Only after 1 minute post-stabilization
                            uint32_t leakRatePerMin = (leakAccumulator * 60000) / timeSinceStable;
                            LOG_ERROR(TASK_TAG, "Leak rate: %lu bytes/minute", 
                                     (unsigned long)leakRatePerMin);
                        }
                    }
                }
            } else if (heapDelta > 5000) {  // Recovered more than 5KB
                LOG_INFO(TASK_TAG, "Memory recovered: %ld bytes", (long)heapDelta);
                // Reduce accumulator but don't go negative
                leakAccumulator = (leakAccumulator > (uint32_t)heapDelta) ? 
                                 leakAccumulator - (uint32_t)heapDelta : 0;
                // Reset consecutive leak counter on recovery
                consecutiveLeaks = 0;
            } else {
                // No significant change - reset consecutive leak counter
                consecutiveLeaks = 0;
            }
        }
        
        lastFreeHeap = freeHeap;
    }
    
    // Check if heap is getting low
    if (freeHeap < WATCHDOG_MIN_HEAP_BYTES) {
        LOG_WARN(TASK_TAG, "Low heap warning: %lu bytes free (minimum: %u)", 
                 (unsigned long)freeHeap, WATCHDOG_MIN_HEAP_BYTES);
    }
    
    // Memory fragmentation check
    size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t fragmentation = 0;
    
    if (freeHeap > 0) {
        fragmentation = 100 - (largestFreeBlock * 100 / freeHeap);
    }
    
    // Get uptime
    uint32_t uptime = millis() / 1000;  // seconds
    uint32_t days = uptime / (24 * 3600);
    uptime %= (24 * 3600);
    uint32_t hours = uptime / 3600;
    uptime %= 3600;
    uint32_t minutes = uptime / 60;
    uint32_t seconds = uptime % 60;
    
    // Get chip info
    uint32_t chipId = ESP.getEfuseMac() & 0xFFFFFFFF;
    uint8_t chipRev = ESP.getChipRevision();
    uint32_t cpuFreq = ESP.getCpuFreqMHz();
    
    // Log the information with reduced formatting complexity
    LOG_INFO(TASK_TAG, "=== System Health Report ===");
    
    // Use snprintf with static buffer to avoid stack allocation
    snprintf(logBuffer, sizeof(logBuffer), "Uptime: %lu days, %02lu:%02lu:%02lu", 
             (unsigned long)days, (unsigned long)hours, 
             (unsigned long)minutes, (unsigned long)seconds);
    LOG_INFO(TASK_TAG, "%s", logBuffer);
    
    snprintf(logBuffer, sizeof(logBuffer), "Free Heap: %lu bytes (%lu%%), Min: %lu bytes", 
             (unsigned long)freeHeap, (unsigned long)heapPercent, (unsigned long)minFreeHeap);
    LOG_INFO(TASK_TAG, "%s", logBuffer);
    
    // Add fragmentation info
    snprintf(logBuffer, sizeof(logBuffer), "Heap Fragmentation: %lu%% (Largest block: %lu bytes)", 
             (unsigned long)fragmentation, (unsigned long)largestFreeBlock);
    LOG_INFO(TASK_TAG, "%s", logBuffer);
    
    // Warn about fragmentation
    if (fragmentation > 50) {
        LOG_WARN(TASK_TAG, "High heap fragmentation detected - consider restart");
    }
    
    snprintf(logBuffer, sizeof(logBuffer), "Chip: ID=0x%08lX, Rev=%u, CPU=%lu MHz", 
             (unsigned long)chipId, chipRev, (unsigned long)cpuFreq);
    LOG_INFO(TASK_TAG, "%s", logBuffer);
    
    // Stack usage monitoring for this task
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    if (stackHighWaterMark < 500) {  // Less than 2KB remaining
        LOG_WARN(TASK_TAG, "Low stack warning: %u words (%u bytes) remaining", 
                 stackHighWaterMark, stackHighWaterMark * 4);
    }
    
    #if defined(LOG_MODE_DEBUG_FULL)
        // Additional debug information
        LOG_DEBUG(TASK_TAG, "Flash Size: %d bytes", ESP.getFlashChipSize());
        LOG_DEBUG(TASK_TAG, "SDK Version: %s", ESP.getSdkVersion());
        
        // Task list with stack monitoring
        const size_t TASK_BUFFER_SIZE = 1536;  // Adequate for task list
        char taskStatusBuffer[TASK_BUFFER_SIZE];  // Stack allocated - auto cleanup
        
        // Clear buffer first
        memset(taskStatusBuffer, 0, TASK_BUFFER_SIZE);
        
        // Get task list
        vTaskList(taskStatusBuffer);
        
        LOG_DEBUG(TASK_TAG, "Task Status:");
        LOG_DEBUG(TASK_TAG, "Name          State  Prio  Stack   Num");
        LOG_DEBUG(TASK_TAG, "----------------------------------------");
        
        // Parse and log each line
        char* saveptr = nullptr;
        char* line = strtok_r(taskStatusBuffer, "\n", &saveptr);
        
        while (line != nullptr && strlen(line) > 0) {
            // Extract task name and stack info for critical tasks
            char taskName[20] = {0};
            char state;
            int prio, stack, num;
            
            if (sscanf(line, "%19s %c %d %d %d", 
                      taskName, &state, &prio, &stack, &num) >= 4) {
                
                // Warn about low stack for any task
                if (stack < 500) {  // Less than 2KB
                    LOG_WARN(TASK_TAG, "LOW STACK: %s has only %d words (%d bytes) free", 
                            taskName, stack, stack * 4);
                }
            }
            
            LOG_DEBUG(TASK_TAG, "%s", line);
            line = strtok_r(nullptr, "\n", &saveptr);
        }
    #endif
}

void MonitoringTask::logNetworkStatus() {
    LOG_INFO(TASK_TAG, "Network Status:");
    
    #ifdef USE_WIFI
        if (WiFi.status() == WL_CONNECTED) {
            LOG_INFO(TASK_TAG, "  WiFi: Connected");
            
            // Use static buffer for IP string formatting
            snprintf(logBuffer, sizeof(logBuffer), "  SSID: %s", WiFi.SSID().c_str());
            LOG_INFO(TASK_TAG, "%s", logBuffer);
            
            snprintf(logBuffer, sizeof(logBuffer), "  IP: %s", WiFi.localIP().toString().c_str());
            LOG_INFO(TASK_TAG, "%s", logBuffer);
            
            snprintf(logBuffer, sizeof(logBuffer), "  RSSI: %d dBm", WiFi.RSSI());
            LOG_INFO(TASK_TAG, "%s", logBuffer);
            
            #if defined(LOG_MODE_DEBUG_SELECTIVE) || defined(LOG_MODE_DEBUG_FULL)
                snprintf(logBuffer, sizeof(logBuffer), "  Gateway: %s", WiFi.gatewayIP().toString().c_str());
                LOG_DEBUG(TASK_TAG, "%s", logBuffer);
                
                snprintf(logBuffer, sizeof(logBuffer), "  DNS: %s", WiFi.dnsIP().toString().c_str());
                LOG_DEBUG(TASK_TAG, "%s", logBuffer);
            #endif
        } else {
            LOG_WARN(TASK_TAG, "  WiFi: Disconnected");
        }
    #else
        LOG_INFO(TASK_TAG, "  Network: Not configured");
    #endif
}

void MonitoringTask::logModuleStatus() {
    LOG_INFO(TASK_TAG, "MB8ART Module Status:");
    
    if (!temperatureModule) {
        LOG_ERROR(TASK_TAG, "  Module: Not initialized!");
        return;
    }
    
    if (temperatureModule->isModuleOffline()) {
        LOG_WARN(TASK_TAG, "  Module: OFFLINE");
        return;
    }
    
    LOG_INFO(TASK_TAG, "  Module: Online");
    LOG_INFO(TASK_TAG, "  Address: 0x%02X", temperatureModule->getServerAddress());
    
    // Get device statistics
    auto stats = temperatureModule->getStatistics();
    LOG_INFO(TASK_TAG, "  Total Requests: %d", stats.totalRequests);
    float successRate = stats.totalRequests > 0 ? 
        (float)stats.successfulRequests / stats.totalRequests * 100.0f : 0.0f;
    LOG_INFO(TASK_TAG, "  Successful: %d (%.1f%%)", 
             stats.successfulRequests, successRate);
    LOG_INFO(TASK_TAG, "  Failed: %d", stats.failedRequests);
    LOG_INFO(TASK_TAG, "  Timeouts: %d", stats.timeouts);
    LOG_INFO(TASK_TAG, "  CRC Errors: %d", stats.crcErrors);
    
    // Connection state based on recent activity
    const char* stateStr = "Unknown";
    if (temperatureModule->isReady()) {
        if (temperatureModule->isModuleResponsive()) {
            stateStr = "Connected";
        } else {
            stateStr = "Disconnected";
        }
    } else {
        stateStr = "Not Initialized";
    }
    LOG_INFO(TASK_TAG, "  Connection State: %s", stateStr);
    
    // Active channels
    int activeChannels = temperatureModule->getActiveChannelCount();
    LOG_INFO(TASK_TAG, "  Active Channels: %d/%d", activeChannels, MB8ART_NUM_CHANNELS);
}

void MonitoringTask::logTemperatureStatistics() {
    LOG_INFO(TASK_TAG, "Temperature Statistics:");
    
    if (!temperatureModule || temperatureModule->isModuleOffline()) {
        LOG_WARN(TASK_TAG, "  No data available");
        return;
    }
    
    float minTemp = 999.0f;
    float maxTemp = -999.0f;
    float avgTemp = 0.0f;
    int validChannels = 0;
    
    // Calculate statistics
    for (int i = 0; i < MB8ART_NUM_CHANNELS; i++) {
        if (temperatureModule->wasSensorLastCommandSuccessful(i)) {
            float temp = temperatureModule->getSensorTemperature(i);
            
            if (temp < minTemp) minTemp = temp;
            if (temp > maxTemp) maxTemp = temp;
            avgTemp += temp;
            validChannels++;
        }
    }
    
    if (validChannels > 0) {
        avgTemp /= validChannels;
        LOG_INFO(TASK_TAG, "  Min: %.1f°C, Max: %.1f°C, Avg: %.1f°C", 
                 minTemp, maxTemp, avgTemp);
        LOG_INFO(TASK_TAG, "  Valid Channels: %d/%d", validChannels, MB8ART_NUM_CHANNELS);
    } else {
        LOG_WARN(TASK_TAG, "  No valid temperature readings");
    }
    
    // Last successful read time
    TickType_t lastRead = TemperatureTask::getLastReadTime();
    if (lastRead > 0) {
        uint32_t timeSinceRead = (xTaskGetTickCount() - lastRead) * portTICK_PERIOD_MS / 1000;
        LOG_INFO(TASK_TAG, "  Last Read: %lu seconds ago", timeSinceRead);
    }
}

void MonitoringTask::logTaskStatus() {
    LOG_INFO(TASK_TAG, "Task Status:");
    
    // List all tasks and their states
    char taskListBuffer[512];
    vTaskList(taskListBuffer);
    
    #if defined(LOG_MODE_DEBUG_FULL)
        LOG_DEBUG(TASK_TAG, "\n%s", taskListBuffer);
    #else
        // In non-debug modes, just count tasks
        int taskCount = uxTaskGetNumberOfTasks();
        LOG_INFO(TASK_TAG, "  Active Tasks: %d", taskCount);
        
        // Check specific task health
        LOG_INFO(TASK_TAG, "  Temperature Task: %s", 
                 TemperatureTask::isRunning() ? "Running" : "Stopped");
        
        uint32_t failures = TemperatureTask::getConsecutiveFailures();
        if (failures > 0) {
            LOG_WARN(TASK_TAG, "  Temperature Read Failures: %d consecutive", failures);
        }
    #endif
}