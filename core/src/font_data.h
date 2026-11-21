#pragma once
#include <array>
#include <cstdint>

namespace pd::os {
// Glyphen für ASCII 32-126, Index = Zeichen - 32.
// Small: 3x5, Bit 14 = oben links, zeilenweise. Large: 5x7, Bit 34 = oben links.
extern const std::array<uint16_t, 95> kFontSmall;
extern const std::array<uint64_t, 95> kFontLarge;
}  // namespace pd::os
