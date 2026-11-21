#pragma once
#include <cstdint>

namespace pd::hal::log {
enum class Level : uint8_t { Debug, Info, Warn, Error };
void write(Level lvl, const char* msg);   // Formatierung macht der Core
}  // namespace pd::hal::log
