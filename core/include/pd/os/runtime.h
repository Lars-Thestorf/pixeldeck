#pragma once
#include <cstddef>
#include <cstdint>

#include "../registry.h"

// Schnittstelle zwischen Plattform (main/app_main) und OS-Kern. Nicht für Spiele.
namespace pd::os {

struct RunOptions {
  const char* gameId = nullptr;  // nullptr: erstes Spiel (Launcher folgt in M2)
  bool        fixedSeed = false;
  uint32_t    seed = 0;
  uint32_t    maxTicks = 0;      // 0 = unbegrenzt
  uint32_t    tickWarnUs = 0;    // 0 = keine Warnung bei langsamen Ticks
};

// Hauptschleife. Rückgabe: 0 = ok, 2 = Spiel nicht gefunden.
int run(const RunOptions& opts);

// Alle registrierten Spiele, alphabetisch nach id (aus game_registry.cpp).
size_t gameCount();
const GameInfo& game(size_t i);
const GameInfo* findGame(const char* id);

// true, solange Spielcode läuft (init/tick/onShutdown). Für den Heap-Debug-Hook am PC.
bool gameCodeActive();

// Formatiertes Log über hal::log
enum class LogLevel : uint8_t { Debug, Info, Warn, Error };
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void logf(LogLevel level, const char* fmt, ...);

}  // namespace pd::os
