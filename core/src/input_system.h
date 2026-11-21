#pragma once
#include <cstddef>
#include <cstdint>

#include "pd/hal/input.h"
#include "pd/input.h"

namespace pd::os {

// Bildet aus den HAL-Geräten den Snapshot für das Spiel (SPEC 6).
// M1: nur Slot 0 = erstes Gerät vom Typ Builtin oder Keyboard.
// Slot-Manager, Join und weitere Slots folgen in M4.
class InputSystem {
public:
  static constexpr size_t MAX_DEVICES = 8;
  static constexpr int STICK_DPAD_THRESHOLD = 64;  // [Annahme] 50 % Auslenkung
  static constexpr uint16_t COMBO_MASK =
      uint16_t(Button::Start) | uint16_t(Button::Select);

  // Rohgeräte holen und Snapshot aufbauen.
  void update(Input& in, int maxPlayers, bool stickAsDpad);
  // Snapshot aus vorgegebenen Geräten aufbauen (ohne HAL, für Tests).
  void apply(Input& in, const hal::input::Device* devs, size_t n, int maxPlayers,
             bool stickAsDpad);

  // true genau in dem Tick, in dem Start+Select auf Slot 0 lange genug gehalten wurden.
  bool menuComboTriggered() const { return comboFired_; }
  void setComboHoldTicks(int t) { comboHoldTicks_ = t; }

  // Kanten zurücksetzen, z. B. beim Spielstart (gehaltene Tasten gelten nicht als "neu").
  void resetEdges(Input& in);

private:
  hal::input::Device devs_[MAX_DEVICES] = {};
  int  comboHeld_ = 0;
  int  comboHoldTicks_ = 60;  // ca. 1 s bei 60 Hz
  bool comboLatched_ = false;
  bool comboFired_ = false;
};

}  // namespace pd::os
