#include <doctest.h>

#include <string>

#include "input_system.h"
#include "profiles.h"
#include "test_hal.h"

using namespace pd;
using pd::os::InputSystem;

namespace {
constexpr uint16_t A = uint16_t(Button::A);

// Ein Tick: Slot-0-Gerät bekommt buttons/Stick, Rest bleibt wie gesetzt.
void step(InputSystem& is, Input& in, uint16_t buttons, int maxPlayers = 1, bool sad = false,
          int8_t lx = 0, int8_t ly = 0) {
  static Input os;
  testhal::device.state.buttons = buttons;
  testhal::device.state.lx = lx;
  testhal::device.state.ly = ly;
  is.update(in, os, maxPlayers, sad);
}

struct Rig {
  InputSystem is;
  Input game, os;
  Rig() { testhal::reset(); }
  void tick(int maxPlayers = 4) { is.update(game, os, maxPlayers, false); }
  void press(int extra) {  // A am Zusatzgerät drücken und loslassen
    testhal::extra[extra].state.buttons = A;
    tick();
    testhal::extra[extra].state.buttons = 0;
  }
};
}  // namespace

TEST_CASE("Input: Flanken") {
  testhal::reset();
  static Input in;
  InputSystem is;
  step(is, in, 0);
  CHECK(in.player(0).connected());
  CHECK_FALSE(in.held(Button::A));

  step(is, in, uint16_t(Button::A));
  CHECK(in.held(Button::A));
  CHECK(in.pressed(Button::A));
  CHECK_FALSE(in.released(Button::A));

  step(is, in, uint16_t(Button::A));
  CHECK(in.held(Button::A));
  CHECK_FALSE(in.pressed(Button::A));

  step(is, in, 0);
  CHECK_FALSE(in.held(Button::A));
  CHECK(in.released(Button::A));
}

TEST_CASE("Input: Slots ohne Gerät und ungültige Slots") {
  testhal::reset();
  static Input in;
  InputSystem is;
  step(is, in, 0);
  CHECK_FALSE(in.player(1).connected());
  CHECK_FALSE(in.player(-1).connected());
  CHECK_FALSE(in.player(99).connected());
  testhal::deviceAttached = false;
  step(is, in, 0);
  CHECK_FALSE(in.player(0).connected());
}

TEST_CASE("Input: Start+Select ist für das Spiel unsichtbar und löst nach Haltezeit aus") {
  testhal::reset();
  static Input in;
  InputSystem is;
  is.setComboHoldTicks(3);
  const uint16_t combo = uint16_t(Button::Start) | uint16_t(Button::Select);
  step(is, in, combo);
  CHECK_FALSE(in.held(Button::Start));
  CHECK_FALSE(in.held(Button::Select));
  CHECK_FALSE(is.menuComboTriggered());
  step(is, in, combo);
  CHECK_FALSE(is.menuComboTriggered());
  step(is, in, combo);
  CHECK(is.menuComboTriggered());
  step(is, in, combo);
  CHECK_FALSE(is.menuComboTriggered());  // nur einmal
  step(is, in, uint16_t(Button::Start));  // Select losgelassen, Start noch gehalten
  CHECK_FALSE(in.held(Button::Start));    // bleibt ausgeblendet
  step(is, in, 0);
  step(is, in, uint16_t(Button::Start));
  CHECK(in.pressed(Button::Start));       // normal wieder sichtbar
}

TEST_CASE("Input: stickAsDpad und maxPlayers") {
  testhal::reset();
  static Input in;
  InputSystem is;
  step(is, in, 0, 1, true, -100, 0);
  CHECK(in.held(Button::Left));
  CHECK(in.player(0).left().x == -100);
  step(is, in, 0, 1, true, 30, 127);
  CHECK_FALSE(in.held(Button::Right));
  CHECK(in.held(Button::Down));
  step(is, in, 0, 1, false, -100, 0);
  CHECK_FALSE(in.held(Button::Left));
}

TEST_CASE("Input: rumble geht an das Gerät des Slots") {
  testhal::reset();
  static Input in;
  InputSystem is;
  step(is, in, 0);
  in.player(0).rumble(120);
  CHECK(testhal::lastRumbleMs == 120);
  CHECK(testhal::lastRumbleId == 7);
}

TEST_CASE("Slots: Join per A in den ersten freien Slot, A erreicht das Spiel nicht") {
  static Rig r;
  r = Rig{};
  testhal::extraCount = 2;
  r.tick();
  CHECK(r.is.joinedMask() == 1);  // Primärgerät in Slot 0
  CHECK(r.is.slot(0).deviceId == 7);
  CHECK_FALSE(r.game.player(1).connected());

  testhal::extra[1].state.buttons = A;
  r.tick();
  CHECK(r.is.joinedMask() == 2);
  CHECK(r.is.slot(1).deviceId == 101);
  CHECK(r.game.player(1).connected());
  CHECK_FALSE(r.game.player(1).held(Button::A));    // Join-A ausgeblendet
  r.tick();
  CHECK_FALSE(r.game.player(1).pressed(Button::A));  // auch solange gehalten
  testhal::extra[1].state.buttons = 0;
  r.tick();
  testhal::extra[1].state.buttons = A;
  r.tick();
  CHECK(r.game.player(1).pressed(Button::A));        // danach normal

  r.press(0);
  CHECK(r.is.slot(2).deviceId == 100);
  CHECK(r.is.presentCount(4) == 3);
  CHECK(r.is.presentCount(2) == 2);
  r.tick(2);
  CHECK_FALSE(r.game.player(2).connected());         // über maxPlayers unsichtbar

  // OS-Sicht: Player 0 ist das Primärgerät, sonst niemand
  CHECK(r.os.player(0).connected());
  CHECK_FALSE(r.os.player(1).connected());
}

TEST_CASE("Slots: ohne allowJoin kein Beitritt") {
  static Rig r;
  r = Rig{};
  testhal::extraCount = 1;
  testhal::extra[0].state.buttons = A;
  r.is.update(r.game, r.os, 4, false, false);
  CHECK_FALSE(r.is.slot(1).used);
}

TEST_CASE("Slots: Trennung reserviert, Rückkehr in denselben Slot, Ablauf gibt frei") {
  static Rig r;
  r = Rig{};
  testhal::extraCount = 2;
  r.tick();
  r.press(0);   // Slot 1
  r.press(1);   // Slot 2
  REQUIRE(r.is.slot(1).deviceId == 100);

  testhal::extra[0].connected = false;
  r.tick();
  CHECK(r.is.lostMask() == 2);
  CHECK(r.is.slot(1).used);
  CHECK_FALSE(r.is.slot(1).present);
  CHECK_FALSE(r.game.player(1).connected());

  testhal::extra[0].connected = true;
  r.tick();
  CHECK(r.is.returnedMask() == 2);
  CHECK(r.game.player(1).connected());

  testhal::extra[0].connected = false;
  for (uint32_t t = 0; t <= InputSystem::HOLD_TICKS; ++t) r.tick();
  CHECK_FALSE(r.is.slot(1).used);
  testhal::extra[0].connected = true;
  r.tick();
  CHECK_FALSE(r.is.slot(1).used);  // braucht wieder A
  r.press(0);
  CHECK(r.is.slot(1).deviceId == 100);
}

TEST_CASE("Slots: Tausch, Entfernen, Primärgerät bleibt") {
  static Rig r;
  r = Rig{};
  testhal::extraCount = 1;
  r.tick();
  r.press(0);
  REQUIRE(r.is.swapSlots(0, 1));
  r.tick();
  CHECK(r.is.slot(0).deviceId == 100);
  CHECK(r.is.primarySlot() == 1);
  // OS folgt dem Primärgerät, nicht Slot 0
  testhal::device.state.buttons = uint16_t(Button::B);
  r.tick();
  CHECK(r.os.player(0).pressed(Button::B));
  CHECK(r.game.player(1).pressed(Button::B));
  CHECK_FALSE(r.game.player(0).held(Button::B));

  CHECK_FALSE(r.is.removeSlot(1));  // Primärgerät
  CHECK(r.is.removeSlot(0));
  r.tick();
  CHECK_FALSE(r.is.slot(0).used);
  CHECK(r.is.primarySlot() == 1);
}

TEST_CASE("Slots: Profil des Geräts wird übernommen, doppelte Profile vermieden") {
  static Rig r;
  r = Rig{};
  static os::Profiles pr;
  pr.clear();
  const uint8_t lars = pr.create("lars", 2);
  pr.remember(7, lars);
  pr.remember(100, lars);  // gleiches Profil auf zweitem Gerät
  r.is.setProfiles(&pr);
  testhal::extraCount = 1;
  r.tick();
  CHECK(r.is.slot(0).profileId == lars);
  CHECK(std::string(r.game.player(0).name()) == "LARS");
  CHECK(r.game.player(0).color() == os::kProfileColor[2]);
  CHECK(r.game.player(0).hasProfile());
  r.press(0);
  CHECK(r.is.slot(1).profileId == os::kGuest);  // LARS ist schon vergeben
  CHECK(std::string(r.game.player(1).name()) == "GUEST");  // Vorgabe Englisch
  CHECK_FALSE(r.game.player(1).hasProfile());

  r.is.profileRemoved(lars);
  r.tick();
  CHECK(r.is.slot(0).profileId == os::kGuest);
  CHECK_FALSE(r.game.player(0).hasProfile());
}

TEST_CASE("Slots: Gäste bekommen eine eigene Farbe, Profile haben Vorrang") {
  static Rig r;
  r = Rig{};
  static os::Profiles pr;
  pr.clear();
  r.is.setProfiles(&pr);
  testhal::extraCount = 2;
  r.tick();
  r.press(0);
  r.press(1);
  // drei Gäste: Vorzugsfarben grün, orange, cyan
  CHECK(r.is.colorIndex(0) == 3);
  CHECK(r.is.colorIndex(1) == 1);
  CHECK(r.is.colorIndex(2) == 4);
  CHECK(r.game.player(1).color() == os::kProfileColor[1]);
  CHECK(r.is.slotColor(3) == os::kGuestColor);  // frei

  // Profil mit Orange in Slot 0: der Gast in Slot 1 weicht auf die nächste freie Farbe aus
  const uint8_t orange = pr.create("anna", 1);
  r.is.setSlotProfile(0, orange);
  r.tick();
  CHECK(r.is.colorIndex(0) == 1);
  CHECK(r.is.colorIndex(1) == 2);
  CHECK(r.is.colorIndex(2) == 4);
  // Vorschau in der Profilwahl: Slot 1 als Gast bzw. mit Profil
  CHECK(r.is.colorFor(1, os::kGuest) == os::kProfileColor[2]);
  CHECK(r.is.colorFor(1, orange) == os::kProfileColor[1]);
}
