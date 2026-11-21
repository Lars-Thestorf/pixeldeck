#pragma once
#include <cstdint>

#include <string>

#include "pd/hal/app.h"
#include "pd/hal/input.h"
#include "pd/hal/power.h"

// Steuerbarer Zustand der Test-HAL
namespace testhal {
extern pd::hal::input::Device device;  // Slot-0-Gerät
extern bool deviceAttached;
extern uint64_t nowUs;
extern int presents;
extern bool quit;
extern uint32_t lastRumbleMs;
extern uint8_t brightness;
extern void (*onPoll)(int frame);      // Eingabe-Skript, frame = bisherige present()-Aufrufe
extern pd::hal::power::PowerState power;
extern int shutdownReadyCalls;
extern pd::hal::app::LastReset nextReset;  // wird beim nächsten lastReset() geliefert
extern std::string activity;              // zuletzt per setActivity gemeldet
void reset();                          // ohne Speicher
void clearStorage();
}  // namespace testhal
