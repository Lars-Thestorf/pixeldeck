#pragma once
#include <cstdint>

#include "pd/hal/input.h"

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
void reset();                          // ohne Speicher
void clearStorage();
}  // namespace testhal
