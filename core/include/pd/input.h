#pragma once
#include <cstdint>

namespace pd {

namespace os { class InputSystem; }

// 16 Buttons als Bitmaske (SPEC 6.1)
enum class Button : uint16_t {
  A = 1 << 0,  B = 1 << 1,  X = 1 << 2,  Y = 1 << 3,
  L = 1 << 4,  R = 1 << 5,  Start = 1 << 6, Select = 1 << 7,
  Up = 1 << 8, Down = 1 << 9, Left = 1 << 10, Right = 1 << 11,
  L3 = 1 << 12, R3 = 1 << 13,
  // Bits 14, 15 reserviert
};

// -127..127, Deadzone/Kalibrierung bereits angewendet.
// Achsen wie der Bildschirm: x > 0 rechts, y > 0 unten.
struct Stick { int8_t x, y; };

class Player {
public:
  bool connected() const { return connected_; }
  bool held(Button b) const { return (cur_ & uint16_t(b)) != 0; }
  bool pressed(Button b) const { return (cur_ & ~prev_ & uint16_t(b)) != 0; }
  bool released(Button b) const { return (~cur_ & prev_ & uint16_t(b)) != 0; }
  uint16_t buttons() const { return cur_; }  // Rohmaske aller gehaltenen Buttons
  Stick left() const { return left_; }
  Stick right() const { return right_; }
  const char* name() const { return name_; }  // Profilname oder "GAST"
  uint16_t color() const { return color_; }   // Profilfarbe
  void rumble(uint16_t ms);                   // No-Op, wenn das Gerät es nicht kann

private:
  friend class os::InputSystem;
  bool connected_ = false;
  uint16_t cur_ = 0, prev_ = 0;
  Stick left_{0, 0}, right_{0, 0};
  uint32_t deviceId_ = 0;
  const char* name_ = "GAST";
  uint16_t color_ = 0xFFFF;
};

// Snapshot, vor jedem tick() gebildet und während des Ticks konstant (SPEC 6.2).
class Input {
public:
  static constexpr int MAX_PLAYERS = 4;

  Player& player(int slot);  // ungültiger Slot: nie verbundener Dummy

  // Kurzformen für Slot 0
  bool held(Button b) const { return players_[0].held(b); }
  bool pressed(Button b) const { return players_[0].pressed(b); }
  bool released(Button b) const { return players_[0].released(b); }

private:
  friend class os::InputSystem;
  Player players_[MAX_PLAYERS];
};

}  // namespace pd
