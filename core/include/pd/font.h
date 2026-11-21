#pragma once
#include <cstdint>

namespace pd {

enum class Font : uint8_t { Small, Large };

struct FontMetrics {
  uint8_t glyphW, glyphH;  // Glyphe in Pixeln
  uint8_t advance;         // horizontaler Vorschub inkl. 1 px Abstand
  uint8_t lineHeight;      // vertikaler Vorschub inkl. 1 px Abstand
};

// SPEC 5.5: Small 3x5 (16x5 Zeichen), Large 5x7 (10x4 Zeichen), Monospace.
constexpr FontMetrics fontMetrics(Font f) {
  return f == Font::Small ? FontMetrics{3, 5, 4, 6} : FontMetrics{5, 7, 6, 8};
}

}  // namespace pd
