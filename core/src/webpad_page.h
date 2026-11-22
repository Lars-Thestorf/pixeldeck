#pragma once
#include <cstddef>
#include <cstdint>

// WebPad-Seite, gzip-komprimiert. Generiert beim Build aus web/webpad.html (tools/pd_embed.py).
namespace pd::os {
extern const uint8_t kWebPadPage[];
extern const size_t kWebPadPageLen;
}  // namespace pd::os
