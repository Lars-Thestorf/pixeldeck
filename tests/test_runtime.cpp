#include <doctest.h>

#include "pd/input.h"
#include "pd/os/runtime.h"
#include "pcg32.h"
#include "test_game.h"
#include "test_hal.h"

using namespace pd;

TEST_CASE("Registry") {
  CHECK(os::gameCount() == 1);
  const GameInfo* g = os::findGame("testgame");
  REQUIRE(g != nullptr);
  CHECK(g->meta.maxPlayers == 2);
  CHECK(g->meta.saveVersion == 1);  // Standardwert
  CHECK(os::findGame("gibtsnicht") == nullptr);
  CHECK(os::findGame(nullptr) == nullptr);
  static_assert(pd::detail::validId("snake"), "");
  static_assert(pd::detail::validId("abc_123"), "");
  static_assert(!pd::detail::validId(""), "");
  static_assert(!pd::detail::validId("Snake"), "");
  static_assert(!pd::detail::validId("a-b"), "");
  static_assert(!pd::detail::validId("abcdefghijklm"), "");  // 13 Zeichen
}

TEST_CASE("Laufzeit: Lebenszyklus, Takt, Arena-Regeln") {
  testhal::reset();
  testgame::stats = {};
  os::RunOptions o;
  o.gameId = "testgame";
  o.fixedSeed = true;
  o.seed = 42;
  o.maxTicks = 120;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.inits == 1);
  CHECK(testgame::stats.ticks == 120);
  CHECK(testgame::stats.lastTick == 119);
  CHECK(testgame::stats.shutdowns == 1);
  CHECK(testgame::stats.destroyed == 1);
  CHECK(testgame::stats.allocInInit);
  CHECK_FALSE(testgame::stats.allocTooBig);
  CHECK_FALSE(testgame::stats.allocInTick);
  CHECK(testhal::presents == 120);
  // 120 Ticks bei 60 Hz ~ 2 s simulierte Zeit
  CHECK(testhal::nowUs >= 1990000);
  CHECK(testhal::nowUs <= 2010000);
  CHECK_FALSE(os::gameCodeActive());

  // gleicher Seed -> gleiche Zufallsfolge
  const uint32_t r1 = testgame::stats.firstRandom;
  testgame::stats = {};
  o.maxTicks = 1;
  os::run(o);
  CHECK(testgame::stats.firstRandom == r1);
  o.seed = 43;
  testgame::stats = {};
  os::run(o);
  CHECK(testgame::stats.firstRandom != r1);
}

TEST_CASE("Laufzeit: unbekanntes Spiel, Quit, Start+Select") {
  testhal::reset();
  os::RunOptions o;
  o.gameId = "nope";
  CHECK(os::run(o) == 2);

  testgame::stats = {};
  o.gameId = nullptr;  // erstes Spiel
  testhal::quit = true;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.ticks == 0);
  CHECK(testgame::stats.shutdowns == 1);

  testhal::reset();
  testgame::stats = {};
  testhal::device.state.buttons = uint16_t(Button::Start) | uint16_t(Button::Select);
  o.maxTicks = 1000;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.ticks < 70);  // ca. 1 s Haltezeit
  CHECK(testgame::stats.shutdowns == 1);
}

TEST_CASE("PCG32 Referenzwerte") {
  // Referenz: pcg32-demo (pcg-random.org), seed 42, seq 54 -> erste Werte
  uint64_t st = 0, inc = 0;
  os::Pcg32::seed(st, inc, 42u, 54u);
  CHECK(os::Pcg32::next(st, inc) == 0xa15c02b7u);
  CHECK(os::Pcg32::next(st, inc) == 0x7b47f409u);
  CHECK(os::Pcg32::next(st, inc) == 0xba1d3330u);
}
