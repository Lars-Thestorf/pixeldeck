#pragma once
#include <cstdint>

namespace pd {

// Sprache der Oberfläche, im Systemmenü einstellbar (SPEC 12.4). Vorgabe Englisch.
enum class Lang : uint8_t { En = 0, De = 1 };
constexpr int kLangCount = 2;

}  // namespace pd
