#pragma once
#include <cstdint>

namespace pd::os {

// OS-Einstellungen im reservierten Namensraum "os" (SPEC 8.2).
struct Settings {
  uint8_t version = 3;
  uint8_t brightnessLevel = 6;  // Index in kBrightnessSteps
  char    lastGame[13] = {};    // zuletzt im Launcher gestartetes Spiel
  // ab Version 2 (M6): WebPad
  uint8_t webpadOn = 0;         // beim Start einschalten
  char    apSuffix[5] = {};     // SSID PIXELDECK-<suffix>, 4 Hex-Ziffern, einmal zufällig erzeugt
  char    apPass[9] = {};       // WPA2-Passwort, 8 Ziffern, einmal zufällig erzeugt
  // ab Version 3: Sprache der Oberfläche (pd::Lang)
  uint8_t lang = 0;             // 0 Englisch, 1 Deutsch
};

constexpr int kBrightnessLevels = 8;
// [Annahme] grob gleichmäßig wahrgenommene Stufen; auf der Matrix nachjustieren
constexpr uint8_t kBrightnessSteps[kBrightnessLevels] = {12, 24, 44, 72, 108, 152, 204, 255};

bool loadSettings(Settings& s);
bool saveSettings(const Settings& s);

}  // namespace pd::os
