#include "settings.h"

#include <cstddef>
#include <cstring>

#include "pd/hal/storage.h"
#include "pd/lang.h"

namespace pd::os {

namespace {
// Ältere Versionen sind Präfixe der aktuellen: Version 1 (bis M5) endet vor webpadOn,
// Version 2 (M6) vor lang. Neue Felder bekommen beim Laden ihre Vorgabe.
constexpr size_t kOldSize[] = {0, offsetof(Settings, webpadOn), offsetof(Settings, lang)};
constexpr uint8_t kOldVersions = uint8_t(sizeof kOldSize / sizeof kOldSize[0]);

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
  if (tmp.version >= 1 && tmp.version < kOldVersions && got == kOldSize[tmp.version]) {
    const Settings def;  // neue Felder auf Vorgabe
    const size_t old = kOldSize[tmp.version];
    std::memcpy(reinterpret_cast<uint8_t*>(&tmp) + old,
                reinterpret_cast<const uint8_t*>(&def) + old, sizeof tmp - old);
    tmp.version = def.version;
  } else if (got != sizeof tmp || tmp.version != Settings{}.version) {
    return false;
  }
  if (tmp.brightnessLevel >= kBrightnessLevels) tmp.brightnessLevel = Settings{}.brightnessLevel;
  tmp.lastGame[sizeof tmp.lastGame - 1] = 0;
  tmp.webpadOn = tmp.webpadOn ? 1 : 0;
  if (tmp.lang >= kLangCount) tmp.lang = 0;
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
