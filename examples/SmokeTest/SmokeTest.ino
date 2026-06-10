// Compile-smoke example for ESP32-MB8ART.
// Purpose: prove the library and its cross-lib dependency chain
// (ESP32-ModbusDevice -> esp32ModbusRTU / ESP32-IDeviceInstance /
// ESP32-MutexGuard / ESP32-LibraryCommon) compile and link under the
// Arduino-ESP32 framework. It is NOT meant to be flashed to hardware.

#include <Arduino.h>
#include <MB8ART.h>

// Construct an MB8ART device at Modbus server address 0x03.
// NOTE: the variable cannot be named `mb8art` — that collides with the
// library's `namespace mb8art`.
static MB8ART device(0x03, "MB8ART-smoke");

void setup() {
  Serial.begin(115200);

  // Exercise a representative slice of the public API without needing a
  // live Modbus bus: construction (above) plus const status/identity
  // queries that are safe to call before initialization.
  const uint8_t addr = device.getServerAddress();
  const bool initialized = device.isInitialized();
  const bool ready = device.isReady();

  Serial.printf("MB8ART addr=0x%02X initialized=%d ready=%d\n",
                addr, initialized ? 1 : 0, ready ? 1 : 0);
}

void loop() {
  // Touch another read-only accessor so it is part of the linked image.
  (void)device.isModuleResponsive();
  delay(1000);
}
