#pragma once
#include <cstdint>

namespace pd::hal::time {
uint64_t micros();                  // monoton
void     sleepUntil(uint64_t micros);
}  // namespace pd::hal::time
