#pragma once
#include <cstdint>

#include <string>
#include <vector>

#include "pd/hal/app.h"
#include "pd/hal/input.h"
#include "pd/hal/net.h"
#include "pd/hal/power.h"

// Steuerbarer Zustand der Test-HAL
namespace testhal {
extern pd::hal::input::Device device;  // Slot-0-Gerät
extern bool deviceAttached;
extern pd::hal::input::Device extra[4];  // weitere Geräte nach dem Slot-0-Gerät
extern int extraCount;
extern uint32_t lastRumbleId;
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
// WebPad-Transport: Ereignisse an den Core, gesendete Nachrichten vom Core
extern uint32_t seedCalls;              // Aufrufe von random::seed() seit reset()
extern bool netStartOk;
extern bool netRunning;
extern std::vector<pd::hal::net::Event> netIn;
struct NetMsg { pd::hal::net::ClientId client; std::vector<uint8_t> data; };
extern std::vector<NetMsg> netOut;
void netOpen(pd::hal::net::ClientId c);
void netClose(pd::hal::net::ClientId c);
void netMsg(pd::hal::net::ClientId c, std::vector<uint8_t> d);
void reset();                          // ohne Speicher
void clearStorage();
}  // namespace testhal
