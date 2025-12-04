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
