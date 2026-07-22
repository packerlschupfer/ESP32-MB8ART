/*
 * CommonModbusDefinitions.h - part of the ESP32-MB8ART library
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

// CommonModbusDefinitions.h
#ifndef COMMON_MODBUS_DEFINITIONS_H
#define COMMON_MODBUS_DEFINITIONS_H

#include <functional>

enum class BaudRate {
    BAUD_1200 = 0,
    BAUD_2400,
    BAUD_4800,
    BAUD_9600,
    BAUD_19200,
    BAUD_38400,
    BAUD_57600,
    BAUD_115200,
    BAUD_FACTORY_RESET,
    ERROR
};

enum class Parity {
    NONE = 0,
    ODD,
    EVEN,
    ERROR
};

struct ModuleSettings {
    uint8_t baudRate;
    uint8_t parity;
    uint8_t rs485Address;
    float moduleTemperature = 0.0f;
    bool isTemperatureValid = false;
};

#endif // COMMON_MODBUS_DEFINITIONS_H
