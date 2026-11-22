#include "settings.h"

#include <cstddef>
#include <cstring>

#include "pd/hal/storage.h"

namespace pd::os {

namespace {
// Version 1 (bis M5) endet vor webpadOn; die Felder davor sind unverändert.
constexpr size_t kSizeV1 = offsetof(Settings, webpadOn);

bool validAp(const Settings& s) {
  for (int i = 0; i < 4; ++i) {
    const char c = s.apSuffix[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  }
  for (int i = 0; i < 8; ++i)
    if (s.apPass[i] < '0' || s.apPass[i] > '9') return false;
  return s.apSuffix[4] == 0 && s.apPass[8] == 0;
}
}  // namespace

bool loadSettings(Settings& s) {
  Settings tmp;
  size_t got = 0;
  if (!hal::storage::load("os", "settings", &tmp, sizeof tmp, &got)) return false;
  if (got == kSizeV1 && tmp.version == 1) {
    const Settings def;  // neue Felder auf Vorgabe
    std::memcpy(reinterpret_cast<uint8_t*>(&tmp) + kSizeV1,
                reinterpret_cast<const uint8_t*>(&def) + kSizeV1, sizeof tmp - kSizeV1);
    tmp.version = def.version;
  } else if (got != sizeof tmp || tmp.version != Settings{}.version) {
    return false;
  }
  if (tmp.brightnessLevel >= kBrightnessLevels) tmp.brightnessLevel = Settings{}.brightnessLevel;
  tmp.lastGame[sizeof tmp.lastGame - 1] = 0;
  tmp.webpadOn = tmp.webpadOn ? 1 : 0;
  if (!validAp(tmp)) {
    tmp.apSuffix[0] = 0;
    tmp.apPass[0] = 0;
  }
  s = tmp;
  return true;
}

bool saveSettings(const Settings& s) {
  return hal::storage::save("os", "settings", &s, sizeof s);
}

}  // namespace pd::os
