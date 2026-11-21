#pragma once
#include <cstdint>

// Akku und Abschalten (SPEC 11, 14.3).
namespace pd::hal::power {

struct PowerState {
  bool     available;        // Gerät misst einen Akku (PC: false)
  uint16_t millivolts;
  uint8_t  percent;          // 0-100
  bool     charging;
  bool     shutdownPending;  // Power-Taste gedrückt, Abschalten steht bevor
};

PowerState read();
void shutdownReady();        // OS hat gesichert, darf abschalten

}  // namespace pd::hal::power
