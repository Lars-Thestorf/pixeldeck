#include "input_system.h"

#include <cstring>

#include "os_call.h"
#include "profiles.h"
#include "webpad.h"

namespace pd {

Player& Input::player(int slot) {
  static Player dummy;  // nie verbunden
  if (slot < 0 || slot >= MAX_PLAYERS) return dummy;
  return players_[slot];
}

void Player::rumble(uint16_t ms) {
  if (!connected_ || ms == 0) return;
  os::OsCallScope osCall;
  if (os::webpadRumble(deviceId_, ms)) return;
  hal::input::rumble(deviceId_, ms);
}

namespace os {

namespace {
constexpr uint16_t kA = uint16_t(Button::A);

uint16_t stickDpad(Stick s) {
  const int t = InputSystem::STICK_DPAD_THRESHOLD;
  uint16_t b = 0;
  if (s.x <= -t) b |= uint16_t(Button::Left);
  if (s.x >= t) b |= uint16_t(Button::Right);
  if (s.y <= -t) b |= uint16_t(Button::Up);
  if (s.y >= t) b |= uint16_t(Button::Down);
  return b;
}
}  // namespace

void InputSystem::reset() {
  for (Dev& d : devs_) d = Dev{};
  for (Slot& s : slots_) s = Slot{};
  primaryId_ = 0;
  hasPrimary_ = false;
  joined_ = lost_ = returned_ = expired_ = 0;
  comboHeld_ = 0;
  comboLatched_ = false;
  comboFired_ = false;
}

void InputSystem::update(Input& game, Input& os, int maxPlayers, bool stickAsDpad,
                         bool allowJoin, const hal::input::Device* extra, size_t nExtra) {
  size_t n = hal::input::poll(raw_, MAX_DEVICES);
  if (n > MAX_DEVICES) n = MAX_DEVICES;
  for (size_t i = 0; i < nExtra && n < MAX_DEVICES; ++i) raw_[n++] = extra[i];
  apply(game, os, raw_, n, maxPlayers, stickAsDpad, allowJoin);
}

int InputSystem::slotOfDevice(uint32_t id) const {
  for (int s = 0; s < SLOTS; ++s)
    if (slots_[s].used && slots_[s].deviceId == id) return s;
  return -1;
}

int InputSystem::primarySlot() const { return hasPrimary_ ? slotOfDevice(primaryId_) : -1; }

int InputSystem::presentCount(int maxSlots) const {
  int c = 0;
  for (int s = 0; s < SLOTS && s < maxSlots; ++s)
    if (slots_[s].used && slots_[s].present) ++c;
  return c;
}

void InputSystem::assign(int s, const Dev& d) {
  Slot& sl = slots_[s];
  sl = Slot{};
  sl.used = true;
  sl.present = true;
  sl.deviceId = d.id;
  sl.type = d.type;
  sl.profileId = profiles_ ? profiles_->lastFor(d.id) : kGuest;
  if (profiles_ && profileInUse(sl.profileId, s)) sl.profileId = kGuest;
  joined_ |= uint8_t(1u << s);
}

bool InputSystem::swapSlots(int a, int b) {
  if (a < 0 || b < 0 || a >= SLOTS || b >= SLOTS || a == b) return false;
  const Slot t = slots_[a];
  slots_[a] = slots_[b];
  slots_[b] = t;
  return true;
}

bool InputSystem::removeSlot(int s) {
  if (s < 0 || s >= SLOTS || !slots_[s].used) return false;
  if (hasPrimary_ && slots_[s].deviceId == primaryId_) return false;
  slots_[s] = Slot{};
  return true;
}

void InputSystem::setSlotProfile(int s, uint8_t profileId) {
  if (s >= 0 && s < SLOTS && slots_[s].used) slots_[s].profileId = profileId;
}

void InputSystem::profileRemoved(uint8_t profileId) {
  for (Slot& s : slots_)
    if (s.used && s.profileId == profileId) s.profileId = kGuest;
}

bool InputSystem::profileInUse(uint8_t profileId, int exceptSlot) const {
  if (profileId == kGuest) return false;
  for (int s = 0; s < SLOTS; ++s)
    if (s != exceptSlot && slots_[s].used && slots_[s].profileId == profileId) return true;
  return false;
}

const Player* InputSystem::devicePlayer(uint32_t id) const {
  for (const Dev& d : devs_)
    if (d.live && d.id == id) return &d.view;
  return nullptr;
}

const char* InputSystem::deviceName(uint32_t id) const {
  for (const Dev& d : devs_)
    if (d.id == id && d.name[0]) return d.name;
  return "?";
}

void InputSystem::fill(Player& p, const Dev* d, bool sad, uint8_t profileId) const {
  p.name_ = profiles_ ? profiles_->name(profileId) : "GAST";
  p.color_ = profiles_ ? profiles_->color(profileId) : kGuestColor;
  if (!d || !d->live) {
    p.connected_ = false;
    p.cur_ = p.prev_ = 0;
    p.left_ = p.right_ = Stick{0, 0};
    if (d) p.deviceId_ = d->id;
    return;
  }
  p.connected_ = true;
  p.deviceId_ = d->id;
  p.cur_ = uint16_t(d->cur | (sad ? stickDpad(d->l) : 0));
  p.prev_ = uint16_t(d->prev | (sad ? stickDpad(d->prevL) : 0));
  p.left_ = d->l;
  p.right_ = d->r;
}

void InputSystem::apply(Input& game, Input& os, const hal::input::Device* in, size_t n,
                        int maxPlayers, bool stickAsDpad, bool allowJoin) {
  using hal::input::DeviceType;
  joined_ = lost_ = returned_ = expired_ = 0;
  comboFired_ = false;

  // 1. Geräte übernehmen
  bool seen[MAX_DEVICES] = {};
  for (size_t i = 0; i < n; ++i) {
    const hal::input::Device& h = in[i];
    if (!h.connected) continue;
    Dev* d = nullptr;
    for (size_t k = 0; k < MAX_DEVICES; ++k)
      if (devs_[k].id == h.id && devs_[k].name[0] && !seen[k]) { d = &devs_[k]; seen[k] = true; break; }
    if (!d) {
      for (size_t k = 0; k < MAX_DEVICES; ++k)
        if (!devs_[k].live && !seen[k]) { devs_[k] = Dev{}; d = &devs_[k]; seen[k] = true; break; }
    }
    if (!d) continue;  // mehr als MAX_DEVICES Geräte
    const bool wasLive = d->live && d->id == h.id;
    const uint16_t raw = h.state.buttons & 0x3FFF;  // Bits 14, 15 reserviert
    d->id = h.id;
    d->type = h.type;
    std::memcpy(d->name, h.name, sizeof d->name);
    d->name[sizeof d->name - 1] = 0;
    if (!d->name[0]) d->name[0] = '?';
    d->hide &= raw;
    const uint16_t b = uint16_t(raw & ~d->hide);
    const Stick l{h.state.lx, h.state.ly};
    if (wasLive) {
      d->prev = d->cur;
      d->prevL = d->l;
    } else {
      d->prev = b;  // frisch verbunden: gehaltene Tasten sind keine Flanken
      d->prevL = l;
      d->hide = 0;
    }
    d->cur = b;
    d->l = l;
    d->r = Stick{h.state.rx, h.state.ry};
    d->live = true;
  }
  for (size_t k = 0; k < MAX_DEVICES; ++k)
    if (!seen[k]) devs_[k].live = false;

  // 2. Primärgerät
  Dev* primary = nullptr;
  for (size_t i = 0; i < n && !primary; ++i)
    if (in[i].connected && (in[i].type == DeviceType::Builtin || in[i].type == DeviceType::Keyboard))
      for (Dev& d : devs_)
        if (d.live && d.id == in[i].id) { primary = &d; break; }
  if (primary) {
    primaryId_ = primary->id;
    hasPrimary_ = true;
  }

  // 3. OS-Kombination Start+Select am Primärgerät: das Spiel sieht sie nie (SPEC 6.2).
  if (primary) {
    uint16_t& b = primary->cur;
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
  } else {
    comboHeld_ = 0;
    comboLatched_ = false;
  }

  // 4. Slots: Trennung, Rückkehr, Ablauf der Reservierung
  for (int s = 0; s < SLOTS; ++s) {
    Slot& sl = slots_[s];
    if (!sl.used) continue;
    const Dev* d = nullptr;
    for (const Dev& x : devs_)
      if (x.live && x.id == sl.deviceId) d = &x;
    if (d) {
      if (!sl.present) returned_ |= uint8_t(1u << s);
      sl.present = true;
      sl.lostTicks = 0;
    } else {
      if (sl.present) {
        lost_ |= uint8_t(1u << s);
        sl.present = false;
        sl.lostTicks = 0;
      } else if (++sl.lostTicks >= HOLD_TICKS) {
        sl = Slot{};
        expired_ |= uint8_t(1u << s);
      }
    }
  }

  auto firstFree = [this]() {
    for (int s = 0; s < SLOTS; ++s)
      if (!slots_[s].used) return s;
    return -1;
  };

  // 5. Primärgerät sitzt immer in einem Slot (Slot 0, wenn frei)
  if (primary && slotOfDevice(primary->id) < 0) {
    const int s = !slots_[0].used ? 0 : firstFree();
    if (s >= 0) assign(s, *primary);
  }

  // 6. Join per A, erster freier Slot. Das A erreicht weder Spiel noch OS.
  if (allowJoin) {
    for (Dev& d : devs_) {
      if (!d.live || &d == primary || slotOfDevice(d.id) >= 0) continue;
      if (!(d.cur & ~d.prev & kA)) continue;
      const int s = firstFree();
      if (s < 0) break;
      assign(s, d);
      d.hide |= kA;
      d.cur &= uint16_t(~kA);
    }
  }

  // 7. Snapshots
  for (Dev& d : devs_) {
    if (!d.live) continue;
    const int s = slotOfDevice(d.id);
    fill(d.view, &d, true, s >= 0 ? slots_[s].profileId : kGuest);
  }
  for (int s = 0; s < SLOTS; ++s) {
    Player& p = game.players_[s];
    const Slot& sl = slots_[s];
    const Dev* d = nullptr;
    if (sl.used && sl.present && s < maxPlayers)
      for (const Dev& x : devs_)
        if (x.live && x.id == sl.deviceId) d = &x;
    fill(p, d, stickAsDpad, sl.used ? sl.profileId : kGuest);
    if (!d) p.deviceId_ = sl.used ? sl.deviceId : 0;
  }
  const int ps = primary ? slotOfDevice(primary->id) : -1;
  fill(os.players_[0], primary, true, ps >= 0 ? slots_[ps].profileId : kGuest);
  for (int s = 1; s < SLOTS; ++s) fill(os.players_[s], nullptr, true, kGuest);
}

void InputSystem::resetEdges(Input& game, Input& os) {
  for (Dev& d : devs_) {
    d.prev = d.cur;
    d.prevL = d.l;
    d.view.prev_ = d.view.cur_;
  }
  for (Player& p : game.players_) p.prev_ = p.cur_;
  for (Player& p : os.players_) p.prev_ = p.cur_;
}

}  // namespace os
}  // namespace pd
