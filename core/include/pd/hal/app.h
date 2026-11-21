#pragma once

// Ergänzung zu SPEC 11 (M1): Die Plattform meldet, dass das Programm beendet
// werden soll (PC: Fenster geschlossen, Esc). Auf dem ESP32 immer false.
namespace pd::hal::app {
bool quitRequested();
}  // namespace pd::hal::app
