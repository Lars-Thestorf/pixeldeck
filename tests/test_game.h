#pragma once
#include <cstdint>

namespace testgame {
struct Stats {
  int inits = 0, ticks = 0, shutdowns = 0, destroyed = 0;
  bool allocInInit = false, allocTooBig = true, allocInTick = true;
  uint32_t lastTick = 0, firstRandom = 0;
  // Storage: Zähler über Läufe hinweg
  bool loaded = false, loadWrongSize = true;
  uint32_t counter = 0;
  // Mehrspieler und Highscores
  bool p2Seen = false;
  int submitAtTick = -1, submitSlot = 0;
  int32_t submitValue = 0;
};
extern Stats stats;
extern int duoTicks;  // Ticks des Zweispieler-Testspiels "duo"
}  // namespace testgame
