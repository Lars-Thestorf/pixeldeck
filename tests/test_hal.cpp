// Minimale Test-HAL: kein Fenster, Zeit läuft nur über sleepUntil.
#include "test_hal.h"

#include <cstdio>
#include <cstring>

#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/time.h"

namespace testhal {
pd::hal::input::Device device{};
bool deviceAttached = true;
uint64_t nowUs = 0;
int presents = 0;
bool quit = false;
uint32_t lastRumbleMs = 0;

void reset() {
  device = {};
  device.id = 7;
  device.type = pd::hal::input::DeviceType::Builtin;
  std::strcpy(device.name, "TEST");
  device.connected = true;
  deviceAttached = true;
  nowUs = 0;
  presents = 0;
  quit = false;
  lastRumbleMs = 0;
}
}  // namespace testhal

namespace pd::hal {
namespace display {
void init() {}
void present(const uint16_t*) { ++testhal::presents; }
void setBrightness(uint8_t) {}
}  // namespace display
namespace input {
size_t poll(Device* out, size_t max) {
  if (!testhal::deviceAttached || max == 0) return 0;
  out[0] = testhal::device;
  return 1;
}
void rumble(uint32_t, uint16_t ms) { testhal::lastRumbleMs = ms; }
}  // namespace input
namespace time {
uint64_t micros() { return testhal::nowUs; }
void sleepUntil(uint64_t t) { if (t > testhal::nowUs) testhal::nowUs = t; }
}  // namespace time
namespace log {
void write(Level, const char*) {}
}  // namespace log
namespace random {
uint32_t seed() { return 1234; }
}  // namespace random
namespace app {
bool quitRequested() { return testhal::quit; }
}  // namespace app
}  // namespace pd::hal
