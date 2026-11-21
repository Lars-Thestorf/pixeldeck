#pragma once
#include <cstdint>

namespace pd::os {

// OS-Einstellungen im reservierten Namensraum "os" (SPEC 8.2).
struct Settings {
  uint8_t version = 1;
  uint8_t brightnessLevel = 6;  // Index in kBrightnessSteps
  char    lastGame[13] = {};    // zuletzt im Launcher gestartetes Spiel
};

constexpr int kBrightnessLevels = 8;
// [Annahme] grob gleichmäßig wahrgenommene Stufen; auf der Matrix nachjustieren
constexpr uint8_t kBrightnessSteps[kBrightnessLevels] = {12, 24, 44, 72, 108, 152, 204, 255};

bool loadSettings(Settings& s);
bool saveSettings(const Settings& s);

}  // namespace pd::os
