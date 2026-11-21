#pragma once
#include <cstddef>
#include <cstdint>

#include "pd/config.h"
#include "pd/hal/input.h"
#include "pd/input.h"

namespace pd::os {

class Profiles;

// Slot-Manager und Snapshots (SPEC 6.2, 6.3).
//
// - Primärgerät = erstes Gerät vom Typ Builtin oder Keyboard in der HAL-Liste (Konsole bzw.
//   Tastatur Layout 1). Es bekommt automatisch einen Slot (Slot 0, falls frei) und bedient das
//   OS (Launcher, Menüs, Start+Select), auch wenn es per Tausch in einem anderen Slot sitzt.
// - Weitere Geräte belegen erst nach A den ersten freien Slot.
// - Getrennte Geräte behalten ihren Slot PD_SLOT_HOLD_MS lang; kommt dieselbe ID zurück,
//   sitzt sie sofort wieder darin.
class InputSystem {
public:
  static constexpr size_t MAX_DEVICES = 8;
  static constexpr int SLOTS = Input::MAX_PLAYERS;
  static constexpr int STICK_DPAD_THRESHOLD = 64;  // [Annahme] 50 % Auslenkung
  static constexpr uint16_t COMBO_MASK =
      uint16_t(Button::Start) | uint16_t(Button::Select);
  static constexpr uint32_t HOLD_TICKS = uint32_t(PD_SLOT_HOLD_MS) * PD_TICK_HZ / 1000;

  struct Slot {
    bool     used = false;      // belegt (verbunden oder reserviert)
    bool     present = false;   // Gerät gerade verbunden
    uint32_t deviceId = 0;
    hal::input::DeviceType type = hal::input::DeviceType::Builtin;
    uint8_t  profileId = 0xFF;  // kGuest
    uint32_t lostTicks = 0;     // Ticks seit der Trennung
  };

  void setProfiles(const Profiles* p) { profiles_ = p; }

  // Rohgeräte holen, Slots pflegen, Snapshots bauen.
  //   game: Sicht des Spiels (Slots, maxPlayers, stickAsDpad)
  //   os:   Sicht der OS-Apps (Player 0 = Primärgerät, Stick immer als D-Pad)
  void update(Input& game, Input& os, int maxPlayers, bool stickAsDpad, bool allowJoin = true);
  // Wie update, aber mit vorgegebenen Geräten (Tests).
  void apply(Input& game, Input& os, const hal::input::Device* devs, size_t n, int maxPlayers,
             bool stickAsDpad, bool allowJoin = true);

  // Ereignisse des letzten update() als Bitmaske je Slot
  uint8_t joinedMask() const { return joined_; }      // per A beigetreten (oder Primärgerät)
  uint8_t lostMask() const { return lost_; }          // Gerät getrennt, Slot reserviert
  uint8_t returnedMask() const { return returned_; }  // Gerät in reservierten Slot zurück
  uint8_t expiredMask() const { return expired_; }    // Reservierung abgelaufen, Slot frei

  const Slot& slot(int s) const { return slots_[s]; }
  int  slotOfDevice(uint32_t id) const;               // -1, wenn ohne Slot
  int  primarySlot() const;                           // -1, wenn das Primärgerät fehlt
  int  presentCount(int maxSlots) const;              // verbundene Slots unter maxSlots
  bool swapSlots(int a, int b);
  bool removeSlot(int s);                             // Primärgerät nicht entfernbar
  void setSlotProfile(int s, uint8_t profileId);
  // Profil wird gelöscht: betroffene Slots auf Gast
  void profileRemoved(uint8_t profileId);
  bool profileInUse(uint8_t profileId, int exceptSlot) const;

  // Eingabe eines einzelnen Geräts (für die Profilwahl beim Join), nullptr = unbekannt
  const Player* devicePlayer(uint32_t id) const;
  const char* deviceName(uint32_t id) const;

  // true genau in dem Tick, in dem Start+Select am Primärgerät lange genug gehalten wurden.
  bool menuComboTriggered() const { return comboFired_; }
  void setComboHoldTicks(int t) { comboHoldTicks_ = t; }

  // Kanten zurücksetzen (beim Start gehaltene Tasten gelten nicht als "neu").
  void resetEdges(Input& game, Input& os);

  // Alles vergessen (Tests, Neustart von run())
  void reset();

private:
  struct Dev {
    bool     live = false;      // in diesem update() von der HAL gemeldet und verbunden
    uint32_t id = 0;
    hal::input::DeviceType type = hal::input::DeviceType::Builtin;
    char     name[16] = {};
    uint16_t cur = 0, prev = 0; // Rohbits ohne Stick-D-Pad, ausgeblendete Bits entfernt
    uint16_t hide = 0;          // ausgeblendet, bis losgelassen (Join-A, Rest der Kombination)
    Stick    l{0, 0}, r{0, 0}, prevL{0, 0};
    Player   view;              // Gerätesicht mit Kanten (devicePlayer)
  };

  void fill(Player& p, const Dev* d, bool sad, uint8_t profileId) const;
  void assign(int s, const Dev& d);

  Dev  devs_[MAX_DEVICES] = {};
  Slot slots_[SLOTS] = {};
  uint32_t primaryId_ = 0;
  bool     hasPrimary_ = false;
  const Profiles* profiles_ = nullptr;
  hal::input::Device raw_[MAX_DEVICES] = {};

  uint8_t joined_ = 0, lost_ = 0, returned_ = 0, expired_ = 0;
  int  comboHeld_ = 0;
  int  comboHoldTicks_ = 60;  // ca. 1 s bei 60 Hz
  bool comboLatched_ = false;
  bool comboFired_ = false;
};

}  // namespace pd::os
