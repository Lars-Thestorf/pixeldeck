#pragma once
#include <cstdint>

namespace pd::hal::display {
void init();
void present(const uint16_t* fb);   // 64*32 RGB565, Zeile für Zeile
void setBrightness(uint8_t level);  // 0-255
}  // namespace pd::hal::display
