#include "input_system.h"

#include "pd/config.h"

namespace pd {

Player& Input::player(int slot) {
  static Player dummy;  // nie verbunden
  if (slot < 0 || slot >= MAX_PLAYERS) return dummy;
  return players_[slot];
}

void Player::rumble(uint16_t ms) {
  if (connected_ && ms > 0) hal::input::rumble(deviceId_, ms);
}

namespace os {

void InputSystem::update(Input& in, int maxPlayers, bool stickAsDpad) {
  size_t n = hal::input::poll(devs_, MAX_DEVICES);
  if (n > MAX_DEVICES) n = MAX_DEVICES;
  apply(in, devs_, n, maxPlayers, stickAsDpad);
}

void InputSystem::apply(Input& in, const hal::input::Device* devs, size_t n, int maxPlayers,
                        bool stickAsDpad) {
  using hal::input::DeviceType;

  for (Player& p : in.players_) p.prev_ = p.cur_;

  const hal::input::Device* slot0 = nullptr;
  for (size_t i = 0; i < n; ++i) {
    if (devs[i].connected &&
        (devs[i].type == DeviceType::Builtin || devs[i].type == DeviceType::Keyboard)) {
      slot0 = &devs[i];
      break;
    }
  }

  // Slots 1-3: in M1 nie belegt.
  for (int s = 1; s < Input::MAX_PLAYERS; ++s) {
    Player& p = in.players_[s];
    p.connected_ = false;
    p.cur_ = 0;
    p.left_ = p.right_ = Stick{0, 0};
  }

  Player& p0 = in.players_[0];
  comboFired_ = false;
  if (!slot0) {
    p0.connected_ = false;
    p0.cur_ = 0;
    p0.left_ = p0.right_ = Stick{0, 0};
    comboHeld_ = 0;
    comboLatched_ = false;
    return;
  }

  const hal::input::RawInput& raw = slot0->state;
  uint16_t b = raw.buttons & 0x3FFF;  // Bits 14, 15 reserviert

  // OS-Kombination Start+Select: das Spiel sieht sie nie (SPEC 6.2).
  if ((b & COMBO_MASK) == COMBO_MASK) {
    if (!comboLatched_ && ++comboHeld_ >= comboHoldTicks_) {
      comboFired_ = true;
      comboLatched_ = true;
    }
    b &= uint16_t(~COMBO_MASK);
  } else {
    comboHeld_ = 0;
    if (comboLatched_) {
      // bis beide losgelassen sind weiter ausblenden, damit kein Rest-Druck ankommt
      if ((b & COMBO_MASK) != 0) b &= uint16_t(~COMBO_MASK);
      else comboLatched_ = false;
    }
  }

  if (stickAsDpad) {
    const int t = STICK_DPAD_THRESHOLD;
    if (raw.lx <= -t) b |= uint16_t(Button::Left);
    if (raw.lx >= t) b |= uint16_t(Button::Right);
    if (raw.ly <= -t) b |= uint16_t(Button::Up);
    if (raw.ly >= t) b |= uint16_t(Button::Down);
  }

  p0.connected_ = maxPlayers >= 1;
  p0.deviceId_ = slot0->id;
  p0.name_ = "GAST";             // Profile folgen in M4
  p0.color_ = 0xFFFF;
  if (p0.connected_) {
    p0.cur_ = b;
    p0.left_ = Stick{raw.lx, raw.ly};
    p0.right_ = Stick{raw.rx, raw.ry};
  } else {
    p0.cur_ = 0;
    p0.left_ = p0.right_ = Stick{0, 0};
  }
}

void InputSystem::resetEdges(Input& in) {
  for (Player& p : in.players_) p.prev_ = p.cur_;
}

}  // namespace os
}  // namespace pd
