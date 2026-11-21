#pragma once
#include <cstdint>

namespace testgame {
struct Stats {
  int inits = 0, ticks = 0, shutdowns = 0, destroyed = 0;
  bool allocInInit = false, allocTooBig = true, allocInTick = true;
  uint32_t lastTick = 0, firstRandom = 0;
};
extern Stats stats;
}  // namespace testgame
