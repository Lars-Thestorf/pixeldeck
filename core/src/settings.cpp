#include "settings.h"

#include "pd/hal/storage.h"

namespace pd::os {

bool loadSettings(Settings& s) {
  Settings tmp;
  size_t got = 0;
  if (!hal::storage::load("os", "settings", &tmp, sizeof tmp, &got)) return false;
  if (got != sizeof tmp || tmp.version != Settings{}.version) return false;
  if (tmp.brightnessLevel >= kBrightnessLevels) tmp.brightnessLevel = Settings{}.brightnessLevel;
  tmp.lastGame[sizeof tmp.lastGame - 1] = 0;
  s = tmp;
  return true;
}

bool saveSettings(const Settings& s) {
  return hal::storage::save("os", "settings", &s, sizeof s);
}

}  // namespace pd::os
