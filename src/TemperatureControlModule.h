/*
 * TemperatureControlModule.h - part of the ESP32-MB8ART library
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