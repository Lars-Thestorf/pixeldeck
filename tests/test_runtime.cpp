#include <doctest.h>

#include <cstring>
#include <string>

#include "pcg32.h"
#include "pd/hal/storage.h"
#include "pd/input.h"
#include "pd/os/runtime.h"
#include "settings.h"
#include "test_game.h"
#include "test_hal.h"

using namespace pd;

namespace {
constexpr uint16_t A = uint16_t(Button::A), DOWN = uint16_t(Button::Down),
                   START = uint16_t(Button::Start), SELECT = uint16_t(Button::Select),
                   RIGHT = uint16_t(Button::Right), UP = uint16_t(Button::Up),
                   X = uint16_t(Button::X), Y = uint16_t(Button::Y);

os::RunOptions direct(uint32_t ticks) {
  os::RunOptions o;
  o.gameId = "testgame";
  o.fixedSeed = true;
  o.seed = 42;
  o.maxTicks = ticks;
  return o;
}
}  // namespace

TEST_CASE("Registry") {
  CHECK(os::gameCount() == 2);
  const GameInfo* g = os::findGame("testgame");
  REQUIRE(g != nullptr);
  CHECK(g->meta.maxPlayers == 2);
  CHECK(g->meta.saveVersion == 3);
  CHECK(g->meta.stickAsDpad == false);  // Standardwert
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
  testhal::clearStorage();
  testgame::stats = {};
  CHECK(os::run(direct(120)) == 0);
  CHECK(testgame::stats.inits == 1);
  CHECK(testgame::stats.ticks == 120);
  CHECK(testgame::stats.lastTick == 119);
  CHECK(testgame::stats.shutdowns == 1);
  CHECK(testgame::stats.destroyed == 1);
  CHECK(testgame::stats.allocInInit);
  CHECK_FALSE(testgame::stats.allocTooBig);
  CHECK_FALSE(testgame::stats.allocInTick);
  CHECK(testhal::presents == 120);
  CHECK(testhal::nowUs >= 1990000);  // 120 Ticks bei 60 Hz ~ 2 s
  CHECK(testhal::nowUs <= 2010000);
  CHECK_FALSE(os::gameCodeActive());

  // gleicher Seed -> gleiche Zufallsfolge
  const uint32_t r1 = testgame::stats.firstRandom;
  testgame::stats = {};
  os::run(direct(1));
  CHECK(testgame::stats.firstRandom == r1);
  os::RunOptions o = direct(1);
  o.seed = 43;
  testgame::stats = {};
  os::run(o);
  CHECK(testgame::stats.firstRandom != r1);
}

TEST_CASE("Storage: Spielstand über Läufe, Größe und Version") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  os::run(direct(1));
  CHECK_FALSE(testgame::stats.loaded);
  os::run(direct(1));
  CHECK(testgame::stats.loaded);
  CHECK(testgame::stats.counter == 1);
  CHECK_FALSE(testgame::stats.loadWrongSize);
  os::run(direct(1));
  CHECK(testgame::stats.counter == 2);

  // Blob liegt im Namensraum "g_<id>", Version im Kopf
  uint8_t raw[64];
  size_t n = 0;
  REQUIRE(hal::storage::load("g_testgame", "save", raw, sizeof raw, &n));
  CHECK(n == 8 + 4);
  uint16_t ver = 0;
  std::memcpy(&ver, raw + 2, 2);
  CHECK(ver == 3);
  ver = 2;  // alte saveVersion -> wird verworfen
  std::memcpy(raw + 2, &ver, 2);
  hal::storage::save("g_testgame", "save", raw, n);
  os::run(direct(1));
  CHECK_FALSE(testgame::stats.loaded);
}

TEST_CASE("Laufzeit: unbekanntes Spiel, Launcher, Quit") {
  testhal::reset();
  os::RunOptions o;
  o.gameId = "nope";
  CHECK(os::run(o) == 2);

  testgame::stats = {};
  o.gameId = nullptr;  // Launcher
  testhal::quit = true;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.inits == 0);
}

TEST_CASE("Begrüßung: A schließt sie, erst das nächste A startet") {
  for (const bool second : {false, true}) {
    testhal::reset();
    testhal::clearStorage();
    testgame::stats = {};
    testhal::onPoll = second ? [](int f) { testhal::device.state.buttons = f == 3 || f == 6 ? A : 0; }
                             : [](int f) { testhal::device.state.buttons = f == 3 ? A : 0; };
    os::RunOptions o;
    o.maxTicks = 30;
    CHECK(os::run(o) == 0);
    CHECK(testgame::stats.inits == (second ? 1 : 0));
  }
}

TEST_CASE("Begrüßung endet nach 2 s von selbst") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  testhal::onPoll = [](int f) { testhal::device.state.buttons = f == 125 ? A : 0; };
  os::RunOptions o;
  o.maxTicks = 140;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.inits == 1);
}

TEST_CASE("Launcher -> Spiel -> Systemmenü -> Beenden -> Launcher") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  static int gameTicksAtMenu;
  gameTicksAtMenu = -1;
  testhal::onPoll = [](int f) {
    uint16_t b = 0;
    if (f == 3) b = A;                                     // Spiel starten
    if (f >= 20 && f < 90) b = START | SELECT;             // Menü öffnen
    if (f == 95) gameTicksAtMenu = testgame::stats.ticks;
    if (f == 100) b = DOWN;                                // auf BEENDEN
    if (f == 110) b = A;
    testhal::device.state.buttons = b;
  };
  os::RunOptions o;
  o.greeting = false;
  o.maxTicks = 150;
  CHECK(os::run(o) == 0);
  CHECK(testgame::stats.inits == 1);
  CHECK(testgame::stats.shutdowns == 1);
  CHECK(testgame::stats.destroyed == 1);
  // Spiel lief von Frame 4 bis Frame 19; das Menü öffnet ohne Haltezeit im ersten Combo-Tick
  CHECK(testgame::stats.ticks > 10);
  CHECK(testgame::stats.ticks < 20);
  CHECK(gameTicksAtMenu == testgame::stats.ticks);

  // zuletzt gestartetes Spiel steht in den OS-Einstellungen
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(std::strcmp(s.lastGame, "testgame") == 0);
}

TEST_CASE("Start+Select: Start einen Tick früher öffnet das Menü nicht gleich wieder") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  static int ticksAfterClose;
  ticksAfterClose = -1;
  testhal::onPoll = [](int f) {
    uint16_t b = 0;
    if (f >= 10 && f < 12) b = START | SELECT;  // Menü auf
    if (f == 40) b = START;                     // Start schließt ...
    if (f > 40 && f < 50) b = START | SELECT;   // ... Select kommt einen Tick später
    if (f == 50) ticksAfterClose = testgame::stats.ticks;
    testhal::device.state.buttons = b;
  };
  CHECK(os::run(direct(80)) == 0);
  CHECK(ticksAfterClose > 15);                     // Spiel lief nach dem Schließen weiter
  CHECK(testgame::stats.ticks > ticksAfterClose);  // und ist nicht wieder pausiert
}

TEST_CASE("Systemmenü: Weiter und Helligkeit werden gespeichert") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  testhal::onPoll = [](int f) {
    uint16_t b = 0;
    if (f >= 10 && f < 75) b = START | SELECT;  // Menü auf
    // WEITER, BEENDEN, SPIELER, PROFILE, REKORDE, WEBPAD, HELL
    if (f == 78 || f == 80 || f == 82 || f == 84 || f == 86 || f == 88) b = DOWN;
    if (f == 90) b = RIGHT;                     // +1 (Standard 6 -> 7)
    if (f == 95) b = uint16_t(Button::B);       // schließen = weiter
    testhal::device.state.buttons = b;
  };
  CHECK(os::run(direct(200)) == 0);
  CHECK(testgame::stats.shutdowns == 1);
  CHECK(testgame::stats.ticks > 100);           // lief nach dem Menü weiter (Frame 96-199)
  CHECK(testhal::brightness == os::kBrightnessSteps[7]);
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(s.brightnessLevel == 7);
}

TEST_CASE("Systemmenü: WebPad-Passwort nur nach Bestätigung, QR-Helligkeit vorübergehend") {
  static bool confirm;
  static char pwOn[9];
  static uint8_t qrBrightness;
  for (bool c : {false, true}) {
    testhal::reset();
    testhal::clearStorage();
    testgame::stats = {};
    confirm = c;
    pwOn[0] = 0;
    qrBrightness = 0;
    testhal::onPoll = [](int f) {
      uint16_t b = 0;
      if (f >= 10 && f < 75) b = START | SELECT;  // Menü auf
      // WEITER, BEENDEN, SPIELER, PROFILE, REKORDE, WEBPAD
      if (f == 78 || f == 80 || f == 82 || f == 84 || f == 86) b = DOWN;
      if (f == 90) b = A;                         // WebPad-Seite
      if (f == 92) b = A;                         // einschalten (speichert)
      if (f == 95) {
        os::Settings s;
        if (os::loadSettings(s)) std::memcpy(pwOn, s.apPass, sizeof pwOn);
      }
      if (f == 96) b = X;                                       // neues Passwort?
      if (f == 98) b = confirm ? A : uint16_t(Button::B);       // ja / nein
      if (f == 106) b = Y;                                      // QR-Ansicht
      if (f == 108) b = UP;                                     // 6 -> 7
      if (f == 110 || f == 112) b = DOWN;                       // -> 5
      if (f == 115) qrBrightness = testhal::brightness;
      if (f == 118) b = START;                                  // Menü zu
      testhal::device.state.buttons = b;
    };
    CHECK(os::run(direct(160)) == 0);
    REQUIRE(pwOn[0] != 0);
    CHECK(qrBrightness == os::kBrightnessSteps[5]);
    CHECK(testhal::brightness == os::kBrightnessSteps[6]);  // nach dem Schließen zurück
    os::Settings s;
    REQUIRE(os::loadSettings(s));
    CHECK(s.brightnessLevel == 6);
    CHECK(s.webpadOn == 1);  // B auf der Bestätigung hat die Seite nicht verlassen
    CHECK((std::strcmp(s.apPass, pwOn) != 0) == c);
  }
}

TEST_CASE("Abschalten: onShutdown, Einstellungen, shutdownReady (SPEC 14.3)") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  testhal::power.available = true;
  testhal::onPoll = [](int frame) { testhal::power.shutdownPending = frame >= 10; };
  CHECK(os::run(direct(60)) == 0);
  CHECK(testgame::stats.ticks == 10);      // ab dem Abschalt-Signal kein Tick mehr
  CHECK(testgame::stats.shutdowns == 1);
  CHECK(testgame::stats.destroyed == 1);
  CHECK(testhal::shutdownReadyCalls == 1);
  CHECK(testhal::activity.empty());
  os::Settings s;
  CHECK(os::loadSettings(s));
}

TEST_CASE("Abschalten bleibt aus: nach 3 s zurück in den Launcher") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  testhal::onPoll = [](int frame) { testhal::power.shutdownPending = frame >= 5 && frame < 8; };
  CHECK(os::run(direct(400)) == 0);
  CHECK(testgame::stats.ticks == 5);
  CHECK(testhal::shutdownReadyCalls == 1);
}

TEST_CASE("Aktivität: Spiel-id während des Spiels, danach leer") {
  testhal::reset();
  testhal::clearStorage();
  static std::string during;
  testhal::onPoll = [](int frame) { if (frame == 3) during = testhal::activity; };
  CHECK(os::run(direct(10)) == 0);
  CHECK(during == "testgame");
  CHECK(testhal::activity.empty());
}

TEST_CASE("Hinweis nach Absturz verdeckt das Spiel, A schließt ihn (SPEC 14.2)") {
  testhal::reset();
  testhal::clearStorage();
  testgame::stats = {};
  testhal::nextReset.cause = hal::app::ResetCause::Crash;
  std::strcpy(testhal::nextReset.gameId, "testgame");
  testhal::onPoll = [](int frame) { testhal::device.state.buttons = frame >= 5 ? A : 0; };
  CHECK(os::run(direct(30)) == 0);
  CHECK(testgame::stats.ticks == 24);  // Ticks 0-5 gehören dem Hinweis

  // ohne Tastendruck: 5 s Anzeige
  testhal::reset();
  testgame::stats = {};
  testhal::nextReset.cause = hal::app::ResetCause::Hang;
  CHECK(os::run(direct(100)) == 0);
  CHECK(testgame::stats.ticks == 0);
}

TEST_CASE("PCG32 Referenzwerte") {
  // Referenz: pcg32-demo (pcg-random.org), seed 42, seq 54 -> erste Werte
  uint64_t st = 0, inc = 0;
  os::Pcg32::seed(st, inc, 42u, 54u);
  CHECK(os::Pcg32::next(st, inc) == 0xa15c02b7u);
  CHECK(os::Pcg32::next(st, inc) == 0x7b47f409u);
  CHECK(os::Pcg32::next(st, inc) == 0xba1d3330u);
}
