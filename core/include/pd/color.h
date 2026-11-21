#pragma once
#include <cstdint>

namespace pd {

// RGB565, native Byte-Reihenfolge (SPEC 5.2)
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Farbschlüssel für Sprites. Reines Magenta ist in Sprites nicht sichtbar nutzbar.
constexpr uint16_t TRANSPARENT = 0xF81F;

namespace color {
constexpr uint16_t BLACK   = rgb(0, 0, 0);
constexpr uint16_t WHITE   = rgb(255, 255, 255);
constexpr uint16_t GRAY    = rgb(96, 96, 96);
constexpr uint16_t RED     = rgb(255, 0, 0);
constexpr uint16_t GREEN   = rgb(0, 255, 0);
constexpr uint16_t BLUE    = rgb(0, 0, 255);
constexpr uint16_t YELLOW  = rgb(255, 255, 0);
constexpr uint16_t CYAN    = rgb(0, 255, 255);
constexpr uint16_t ORANGE  = rgb(255, 128, 0);
constexpr uint16_t PURPLE  = rgb(160, 0, 255);
}  // namespace color

}  // namespace pd
