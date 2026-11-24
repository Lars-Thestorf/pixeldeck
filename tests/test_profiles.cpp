// M4: Profile, Highscores, Lobby, Join im Spiel, Systemmenü-Seiten
#include <doctest.h>

#include <cstring>
#include <string>

#include "pd/input.h"
#include "pd/os/runtime.h"
#include "lang_impl.h"
#include "profiles.h"
#include "score_table.h"
#include "test_game.h"
#include "test_hal.h"

using namespace pd;

namespace {
constexpr uint16_t A = uint16_t(Button::A), B = uint16_t(Button::B),
                   UP = uint16_t(Button::Up), DOWN = uint16_t(Button::Down),
                   RIGHT = uint16_t(Button::Right), START = uint16_t(Button::Start);

os::RunOptions direct(const char* id, uint32_t ticks) {
  os::RunOptions o;
  o.gameId = id;
  o.fixedSeed = true;
  o.seed = 1;
  o.maxTicks = ticks;
  return o;
}
}  // namespace

TEST_CASE("Profile: anlegen, ändern, löschen, speichern") {
  testhal::reset();
  testhal::clearStorage();
  os::Profiles p;
  p.clear();
  CHECK(p.create("", 0) == 0);        // leerer Name
  CHECK(p.create("   ", 0) == 0);
  const uint8_t a = p.create("lars", 3);
  const uint8_t b = p.create("ANNA-123456", 9);  // gekürzt auf 8, Farbe modulo
  REQUIRE(a != 0);
  REQUIRE(b != 0);
  CHECK(a != b);
  CHECK(std::string(p.name(a)) == "LARS");
  CHECK(std::string(p.name(b)) == "ANNA-123");
  CHECK(p.color(b) == os::kProfileColor[1]);
  os::g_lang = Lang::En;
  CHECK(std::string(p.name(os::kGuest)) == "GUEST");
  os::g_lang = Lang::De;
  CHECK(std::string(p.name(os::kGuest)) == "GAST");
  os::g_lang = Lang::En;
  CHECK(p.color(os::kGuest) == os::kGuestColor);
  CHECK(p.update(a, "LARS T ", 4));
  CHECK(std::string(p.name(a)) == "LARS T");
  p.remember(55, b);
  CHECK(p.lastFor(55) == b);
  CHECK(p.lastFor(56) == os::kGuest);
  REQUIRE(p.save());

  os::Profiles q;
  REQUIRE(q.load());
  CHECK(q.count() == 2);
  CHECK(std::string(q.name(a)) == "LARS T");
  CHECK(q.lastFor(55) == b);
  CHECK(q.remove(b));
  CHECK(q.count() == 1);
  CHECK(q.lastFor(55) == os::kGuest);  // Gerät fällt auf Gast zurück
  // neue ID wiederholt nicht sofort die gelöschte
  const uint8_t c = q.create("NEU", 0);
  CHECK(c != b);
  CHECK(c != a);

  // voll
  os::Profiles f;
  f.clear();
  for (int i = 0; i < os::Profiles::MAX; ++i) CHECK(f.create("X", 0) != 0);
  CHECK(f.create("X", 0) == 0);
}

TEST_CASE("Highscores: Reihenfolge, Gleichstand, Top 10, Modus") {
  os::ScoreTable t;
  CHECK(t.insert(ScoreMode::Higher, 0, 1, "A") == -1);   // [Annahme] 0 zählt nicht
  CHECK(t.insert(ScoreMode::None, 5, 1, "A") == -1);
  CHECK(t.insert(ScoreMode::Higher, 10, 1, "A") == 0);
  CHECK(t.insert(ScoreMode::Higher, 30, 2, "B") == 0);
  CHECK(t.insert(ScoreMode::Higher, 10, 3, "C") == 2);   // Gleichstand: älterer vorn
  CHECK(t.count == 3);
  CHECK(std::string(t.e[0].name) == "B");
  CHECK(std::string(t.e[1].name) == "A");
  for (int i = 0; i < 10; ++i) t.insert(ScoreMode::Higher, 100 + i, 4, "D");
  CHECK(t.count == 10);
  CHECK(t.e[0].score == 109);
  CHECK(t.e[9].score == 100);  // 30 ist herausgefallen
  CHECK(t.insert(ScoreMode::Higher, 5, 4, "D") == -1);

  os::ScoreTable l;
  CHECK(l.insert(ScoreMode::Lower, 50, 1, "A") == 0);
  CHECK(l.insert(ScoreMode::Lower, 20, 1, "A") == 0);
  CHECK(l.insert(ScoreMode::Lower, -3, 1, "A") == 0);    // bei Lower zählt alles
  CHECK(l.e[2].score == 50);
}

TEST_CASE("Highscores: submit aus dem Spiel geht an das Profil im Slot") {
  testhal::reset();
  testhal::clearStorage();
  os::Profiles p;
  p.clear();
  const uint8_t id = p.create("LARS", 0);
  p.remember(7, id);  // Testgerät
  p.save();

  testgame::stats = {};
  testgame::stats.submitAtTick = 5;
  testgame::stats.submitValue = 120;
  CHECK(os::run(direct("testgame", 10)) == 0);
  testgame::stats = {};
  testgame::stats.submitAtTick = 5;
  testgame::stats.submitValue = 80;
  testgame::stats.submitSlot = 3;  // leerer Slot -> Gast
  CHECK(os::run(direct("testgame", 10)) == 0);

  os::ScoreTable t;
  REQUIRE(t.load("testgame"));
  CHECK(t.count == 2);
  CHECK(t.e[0].score == 120);
  CHECK(t.e[0].profileId == id);
  CHECK(std::string(t.e[0].name) == "LARS");
  CHECK(std::string(t.e[1].name) == "GUEST");
  CHECK(os::ScoreTable::reset("testgame"));
  CHECK_FALSE(t.load("testgame"));
}

TEST_CASE("Lobby: wartet auf den zweiten Spieler, Profilwahl, dann Start (SPEC 6.4)") {
  testhal::reset();
  testhal::clearStorage();
  testgame::duoTicks = 0;
  static int ticksBeforeJoin;
  ticksBeforeJoin = -1;
  testhal::onPoll = [](int f) {
    testhal::extraCount = 1;
    testhal::extra[0].state.buttons = (f == 5 || f == 9) ? A : 0;  // Join, dann Profil bestätigen
    if (f == 8) ticksBeforeJoin = testgame::duoTicks;
  };
  CHECK(os::run(direct("duo", 30)) == 0);
  CHECK(ticksBeforeJoin == 0);
  CHECK(testgame::duoTicks > 10);
  CHECK(testgame::duoTicks < 22);

  // B am Primärgerät bricht die Lobby ab
  testhal::reset();
  testgame::duoTicks = 0;
  testhal::onPoll = [](int f) { testhal::device.state.buttons = f == 5 ? B : 0; };
  CHECK(os::run(direct("duo", 30)) == 0);
  CHECK(testgame::duoTicks == 0);
}

TEST_CASE("Join im laufenden Spiel ohne Auswahl, letztes Profil des Geräts (SPEC 7.1)") {
  testhal::reset();
  testhal::clearStorage();
  os::Profiles p;
  p.clear();
  const uint8_t id = p.create("ANNA", 1);
  p.remember(100, id);
  p.save();
  testgame::stats = {};
  testgame::stats.submitAtTick = 30;
  testgame::stats.submitSlot = 1;
  testgame::stats.submitValue = 7;
  testhal::onPoll = [](int f) {
    testhal::extraCount = 1;
    testhal::extra[0].state.buttons = f == 10 ? A : 0;
  };
  CHECK(os::run(direct("testgame", 40)) == 0);
  CHECK(testgame::stats.ticks == 40);  // keine Unterbrechung
  CHECK(testgame::stats.p2Seen);
  os::ScoreTable t;
  REQUIRE(t.load("testgame"));
  CHECK(std::string(t.e[0].name) == "ANNA");
}

TEST_CASE("Systemmenü: Profil anlegen über den Editor") {
  testhal::reset();
  testhal::clearStorage();
  testhal::onPoll = [](int f) {
    uint16_t b = 0;
    switch (f) {
      case 3: b = START; break;   // Menü: SPIELER, PROFILE, ...
      case 5: b = DOWN; break;    // PROFILE
      case 7: b = A; break;       // Liste (nur "+ NEU")
      case 9: b = A; break;       // Editor, Name "A"
      case 11: b = UP; break;     // "B"
      case 13: b = RIGHT; break;
      case 15: b = UP; break;     // "BA"
      case 17: b = A; break;      // speichern
      default: break;
    }
    testhal::device.state.buttons = b;
  };
  os::RunOptions o;
  o.greeting = false;
  o.maxTicks = 25;
  CHECK(os::run(o) == 0);
  os::Profiles p;
  REQUIRE(p.load());
  REQUIRE(p.count() == 1);
  CHECK(std::string(p.at(0).name) == "BA");
}
