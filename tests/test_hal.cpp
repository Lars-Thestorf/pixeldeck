// Minimale Test-HAL: kein Fenster, Zeit läuft nur über sleepUntil, Speicher im RAM.
#include "test_hal.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/storage.h"
#include "pd/hal/time.h"

namespace testhal {
pd::hal::input::Device device{};
bool deviceAttached = true;
uint64_t nowUs = 0;
int presents = 0;
bool quit = false;
uint32_t lastRumbleMs = 0;
uint8_t brightness = 0;
void (*onPoll)(int) = nullptr;

struct Entry { std::string name; std::vector<uint8_t> data; };
std::vector<Entry> store;

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
  onPoll = nullptr;
}
void clearStorage() { store.clear(); }

Entry* find(const char* ns, const char* key) {
  const std::string n = std::string(ns) + "/" + key;
  for (Entry& e : store)
    if (e.name == n) return &e;
  return nullptr;
}
}  // namespace testhal

namespace pd::hal {
namespace display {
void init() {}
void present(const uint16_t*) { ++testhal::presents; }
void setBrightness(uint8_t b) { testhal::brightness = b; }
}  // namespace display
namespace input {
size_t poll(Device* out, size_t max) {
  if (testhal::onPoll) testhal::onPoll(testhal::presents);
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
namespace storage {
bool load(const char* ns, const char* key, void* buf, size_t len, size_t* outLen) {
  testhal::Entry* e = testhal::find(ns, key);
  if (outLen) *outLen = 0;
  if (!e) return false;
  if (outLen) *outLen = e->data.size();
  if (e->data.size() <= len) std::memcpy(buf, e->data.data(), e->data.size());
  return true;
}
bool save(const char* ns, const char* key, const void* buf, size_t len) {
  testhal::Entry* e = testhal::find(ns, key);
  if (!e) {
    testhal::store.push_back({std::string(ns) + "/" + key, {}});
    e = &testhal::store.back();
  }
  e->data.assign(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
  return true;
}
bool erase(const char* ns, const char* key) {
  const std::string n = std::string(ns) + "/" + key;
  for (size_t i = 0; i < testhal::store.size(); ++i)
    if (testhal::store[i].name == n) {
      testhal::store.erase(testhal::store.begin() + long(i));
      return true;
    }
  return false;
}
}  // namespace storage
}  // namespace pd::hal
