// src/TemperatureControlModule.cpp

#include "TemperatureControlModule.h"
#include "MB8ART.h"
#include "MB8ARTSharedResources.h"

// Constructor implementation
TemperatureControlModule::TemperatureControlModule() {
    // Simple initialization, no logging to avoid initialization order issues
}

// Destructor implementation 
TemperatureControlModule::~TemperatureControlModule() {
    // Simple cleanup, no logging to avoid shutdown order issues
}

#ifdef MB8ART_ENABLE_MQTT
void TemperatureControlModule::handleMessage(const std::string& topic, const std::string& payload) {
    LOG_MB8ART_INFO_NL("Received MQTT message on topic: %s", topic.c_str());
    LOG_MB8ART_INFO_NL("Message payload: %s", payload.c_str());

    if (topic == "sensors/control") {
        // Parse commands like "configure_range:high" or "read_temperature"
        if (payload == "read_temperature") {
            readTemperature();
        } 
        else if (payload.find("configure_range:") == 0) {
            std::string rangeStr = payload.substr(16); // Extract after "configure_range:"
            configureMeasurementRange(rangeStr);
        }
        else {
            handleControlCommand(payload);
        }
    }
}
#endif

void TemperatureControlModule::readTemperature() {
    if (MB8ART_SRP_MB8ART == nullptr) {
        LOG_MB8ART_ERROR_NL("MB8ART instance not available - cannot read temperature");
        return;
    }
    
    if (!MB8ART_SRP_MB8ART->isInitialized()) {
        LOG_MB8ART_ERROR_NL("MB8ART instance not initialized - cannot read temperature");
        return;
    }
    
    MB8ART_SRP_MB8ART->requestAllData();
    LOG_MB8ART_DEBUG_NL("Requesting temperature data via control module");
}

void TemperatureControlModule::configureMeasurementRange(const std::string& range) {
    if (MB8ART_SRP_MB8ART == nullptr) {
        LOG_MB8ART_ERROR_NL("MB8ART instance not available - cannot configure range");
        return;
    }
    
    if (!MB8ART_SRP_MB8ART->isInitialized()) {
        LOG_MB8ART_ERROR_NL("MB8ART instance not initialized - cannot configure range");
        return;
    }

    if (range == "high") {
        MB8ART_SRP_MB8ART->configureMeasurementRange(mb8art::MeasurementRange::HIGH_RES);
        LOG_MB8ART_DEBUG_NL("Configured measurement range to HIGH_RES");
    } 
    else if (range == "low") {
        MB8ART_SRP_MB8ART->configureMeasurementRange(mb8art::MeasurementRange::LOW_RES);
        LOG_MB8ART_DEBUG_NL("Configured measurement range to LOW_RES");
    }
    else {
        LOG_MB8ART_ERROR_NL("Invalid measurement range: %s", range.c_str());
    }
}

void TemperatureControlModule::handleControlCommand(const std::string& command, const std::string& parameter) {
    LOG_MB8ART_DEBUG_NL("Processing control command: %s", command.c_str());
    
    if (command == "read_temperature") {
        readTemperature();
    }
    else if (command == "configure_range") {
        configureMeasurementRange(parameter);
    }
    else if (command == "print_settings") {
        if (MB8ART_SRP_MB8ART == nullptr) {
            LOG_MB8ART_ERROR_NL("MB8ART instance not available - cannot print settings");
            return;
        }
        if (!MB8ART_SRP_MB8ART->isInitialized()) {
            LOG_MB8ART_ERROR_NL("MB8ART instance not initialized - cannot print settings");
            return;
        }
        MB8ART_SRP_MB8ART->printModuleSettings();
    }
    else if (command == "print_readings") {
        if (MB8ART_SRP_MB8ART == nullptr) {
            LOG_MB8ART_ERROR_NL("MB8ART instance not available - cannot print readings");
            return;
        }
        if (!MB8ART_SRP_MB8ART->isInitialized()) {
            LOG_MB8ART_ERROR_NL("MB8ART instance not initialized - cannot print readings");
            return;
        }
        const mb8art::SensorReading* readings = MB8ART_SRP_MB8ART->getSensorReadings();
        if (readings == nullptr) {
            LOG_MB8ART_ERROR_NL("Sensor readings not available");
            return;
        }
        for (int i = 0; i < DEFAULT_NUMBER_OF_SENSORS; i++) {
            MB8ART_SRP_MB8ART->printSensorReading(readings[i], i);
        }
    }
    else {
        LOG_MB8ART_ERROR_NL("Unknown control command: %s", command.c_str());
    }
}