#include <doctest.h>

#include "input_system.h"
#include "test_hal.h"

using namespace pd;
using pd::os::InputSystem;

namespace {
void step(InputSystem& is, Input& in, uint16_t buttons, int maxPlayers = 1, bool sad = false,
          int8_t lx = 0, int8_t ly = 0) {
  testhal::device.state.buttons = buttons;
  testhal::device.state.lx = lx;
  testhal::device.state.ly = ly;
  is.update(in, maxPlayers, sad);
}
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
}
