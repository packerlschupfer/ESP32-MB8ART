// src/TemperatureControlModule.h

#ifndef TEMPERATURECONTROLMODULE_H
#define TEMPERATURECONTROLMODULE_H

#include <string>
#include "MB8ARTLoggingMacros.h"

// Optional MQTT support - define MB8ART_ENABLE_MQTT in your build flags to enable
#ifdef MB8ART_ENABLE_MQTT
#include "IMqttMessageHandler.h"
#endif

#ifdef MB8ART_ENABLE_MQTT
class TemperatureControlModule : public IMqttMessageHandler {
#else
class TemperatureControlModule {
#endif

public:
    TemperatureControlModule();
    virtual ~TemperatureControlModule();

#ifdef MB8ART_ENABLE_MQTT
    // MQTT message handling (when MQTT is enabled)
    void handleMessage(const std::string& topic, const std::string& payload) override;
#endif

    // Direct control methods (always available)
    void readTemperature();
    void configureMeasurementRange(const std::string& range);
    void handleControlCommand(const std::string& command, const std::string& parameter = "");

private:
    // Add any additional private members, methods, or variables here
};

#endif // TEMPERATURECONTROLMODULE_H