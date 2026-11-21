#pragma once
#include <cstdint>

// Ergänzung zu SPEC 11: Programmlauf, Watchdog und Absturzmeldung (SPEC 14.2).
namespace pd::hal::app {

// Die Plattform meldet, dass das Programm beendet werden soll
// (PC: Fenster geschlossen, Esc). Auf dem ESP32 immer false.
bool quitRequested();

// Einmal pro Durchlauf der Hauptschleife. ESP32: Task-Watchdog bedienen.
void heartbeat();

// Laufendes Spiel (id) oder nullptr für das OS. Die Plattform legt den Wert so ab,
// dass er einen Watchdog-Reset oder Absturz übersteht (ESP32: RTC-Speicher).
void setActivity(const char* gameId);

enum class ResetCause : uint8_t { None, Hang, Crash };
struct LastReset {
  ResetCause cause;
  char       gameId[13];  // leer = OS
};
// Grund des letzten Neustarts. Liefert nach einem Boot höchstens einmal != None.
LastReset lastReset();

}  // namespace pd::hal::app
