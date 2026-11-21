#pragma once
#include <cstddef>
#include <cstdint>

namespace pd {

// Sprite im Flash: frames * w * h Pixel RGB565, Frames hintereinander (SPEC 5.4).
struct Sprite {
  uint8_t w, h;
  uint16_t frames;
  const uint16_t* data;

  // Einzelner Frame als eigenes Sprite. n wird modulo frames genommen,
  // damit frame(ctx.tick() / 8) direkt als Animation funktioniert.
  constexpr Sprite frame(uint16_t n) const {
    return frames == 0 ? Sprite{w, h, 0, data}
                       : Sprite{w, h, 1, data + size_t(n % frames) * w * h};
  }
};

enum : uint8_t { FLIP_X = 1 << 0, FLIP_Y = 1 << 1 };

}  // namespace pd
