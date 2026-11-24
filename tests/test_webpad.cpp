// M6: WebPad-Protokoll im Core (SPEC 13) gegen den Test-Transport der Test-HAL
#include <doctest.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "pd/hal/storage.h"
#include "pd/input.h"
#include "pd/os/runtime.h"
#include "lang_impl.h"
#include "profiles.h"
#include "score_table.h"
#include "settings.h"
#include "test_game.h"
#include "test_hal.h"
#include "webpad.h"

using namespace pd;
using os::WebPad;

namespace {
constexpr uint16_t A = uint16_t(Button::A);

os::RunOptions direct(const char* id, uint32_t ticks) {
  os::RunOptions o;
  o.gameId = id;
  o.fixedSeed = true;
  o.seed = 1;
  o.maxTicks = ticks;
  return o;
}

void enableWebPad() {
  os::Settings s;
  s.webpadOn = 1;
  os::saveSettings(s);
}

std::vector<uint8_t> hello(uint8_t tokenByte, uint8_t profile, uint8_t version = WebPad::kVersion) {
  std::vector<uint8_t> m{WebPad::HELLO, version};
  for (int i = 0; i < 16; ++i) m.push_back(uint8_t(tokenByte + i));
  m.push_back(profile);
  return m;
}

std::vector<uint8_t> state(uint16_t b, int8_t lx = 0) {
  return {WebPad::STATE, uint8_t(b & 0xFF), uint8_t(b >> 8), uint8_t(lx), 0, 0, 0};
}

// Letzte Nachricht eines Typs an einen Client, leer wenn keine
std::vector<uint8_t> lastOf(uint32_t client, uint8_t type) {
  for (auto it = testhal::netOut.rbegin(); it != testhal::netOut.rend(); ++it)
    if (it->client == client && !it->data.empty() && it->data[0] == type) return it->data;
  return {};
}
}  // namespace

TEST_CASE("WebPad: HELLO, Beitritt per A mit Profil vom Handy (SPEC 13.2, 7.1)") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  os::Profiles p;
  p.clear();
  static uint8_t anna;
  anna = p.create("ANNA", 4);
  p.save();

  testgame::stats = {};
  testgame::stats.submitAtTick = 40;
  testgame::stats.submitSlot = 1;
  testgame::stats.submitValue = 99;
  testhal::onPoll = [](int f) {
    if (f == 2) {
      testhal::netOpen(5);
      testhal::netMsg(5, hello(10, anna));
    }
    if (f > 2 && f % 10 == 0) testhal::netMsg(5, state(0));  // Heartbeat
    if (f == 15) testhal::netMsg(5, state(A));
  };
  CHECK(os::run(direct("testgame", 50)) == 0);
  CHECK(testhal::netRunning == false);  // run() stoppt das Netz am Ende

  const auto w = lastOf(5, WebPad::WELCOME);
  REQUIRE(w.size() == 4 + 2 * os::kProfileColors);
  CHECK(w.back() == uint8_t(Lang::En));  // Sprache als letztes Byte (Vorgabe Englisch)
  CHECK(w[1] == WebPad::kVersion);
  CHECK(w[2] == os::kProfileColors);
  CHECK((w[3] | (w[4] << 8)) == os::kProfileColor[0]);

  const auto pl = lastOf(5, WebPad::PROFILES);
  REQUIRE(pl.size() == 2 + 10);
  CHECK(pl[1] == 1);
  CHECK(pl[2] == anna);
  CHECK(pl[3] == 4);
  CHECK(std::string(reinterpret_cast<const char*>(&pl[4]), 4) == "ANNA");

  const auto sl = lastOf(5, WebPad::SLOT);
  REQUIRE(sl.size() == 4);
  CHECK(sl[1] == 1);  // Slot 2
  CHECK(sl[2] == anna);
  CHECK(sl[3] == 4);  // Farbindex des Profils
  CHECK(testgame::stats.p2Seen);

  os::ScoreTable t;
  REQUIRE(t.load("testgame"));
  CHECK(std::string(t.e[0].name) == "ANNA");
}

TEST_CASE("WebPad: Timeout trennt, gleiches Token bekommt seinen Slot zurück (SPEC 6.3)") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testhal::onPoll = [](int f) {
    if (f == 2) { testhal::netOpen(5); testhal::netMsg(5, hello(1, os::kGuest)); }
    if (f == 10) testhal::netMsg(5, state(A));
    if (f == 12) testhal::netMsg(5, state(0));
    // ab Frame 12 schweigt das Handy; nach 1 s gilt es als getrennt
    if (f == 120) {
      testhal::netClose(5);
      testhal::netOpen(6);
      testhal::netMsg(6, hello(1, os::kGuest));
      testhal::netMsg(6, state(0));
    }
  };
  CHECK(os::run(direct("testgame", 130)) == 0);
  // erste Verbindung: Slot 2, dann (nach Timeout) Rückkehr über eine neue Verbindung
  const auto s5 = lastOf(5, WebPad::SLOT);
  REQUIRE(s5.size() == 4);
  CHECK(s5[1] == 1);
  CHECK(s5[3] < os::kProfileColors);  // auch der Gast hat eine Farbe
  const auto s6 = lastOf(6, WebPad::SLOT);
  REQUIRE(s6.size() == 4);
  CHECK(s6[1] == 1);  // ohne erneutes A wieder im reservierten Slot
}

TEST_CASE("WebPad: Funkloch über 1 s lässt Tasten los, getrennt erst nach 2,5 s") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testgame::stats = {};
  static bool heldBefore, connectedStale, releasedStale, connectedLate;
  heldBefore = connectedStale = releasedStale = false;
  connectedLate = true;
  testhal::onPoll = [](int f) {
    if (f == 2) { testhal::netOpen(5); testhal::netMsg(5, hello(1, os::kGuest)); }
    if (f == 5) testhal::netMsg(5, state(A));   // Beitritt
    if (f == 6) testhal::netMsg(5, state(0));
    if (f == 10) testhal::netMsg(5, state(A));  // A halten, dann schweigt das Handy
    if (f == 30) heldBefore = testgame::stats.p2Buttons & A;
    if (f == 10 + 90) {                         // 1,5 s still
      connectedStale = testgame::stats.p2Connected;
      releasedStale = testgame::stats.p2Buttons == 0;
    }
    if (f == 10 + 170) connectedLate = testgame::stats.p2Connected;  // 2,8 s still
  };
  CHECK(os::run(direct("testgame", 190)) == 0);
  CHECK(heldBefore);
  CHECK(connectedStale);
  CHECK(releasedStale);
  CHECK_FALSE(connectedLate);
}

TEST_CASE("WebPad: Profil auf dem Handy anlegen und wählen") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testhal::onPoll = [](int f) {
    if (f == 2) { testhal::netOpen(9); testhal::netMsg(9, hello(50, os::kGuest)); }
    if (f == 5) testhal::netMsg(9, {WebPad::PROFILE_CREATE, 'b', 'e', 'n', 0, 0, 0, 0, 0, 3});
    if (f == 8) testhal::netMsg(9, {WebPad::PROFILE_CREATE, ' ', 0, 0, 0, 0, 0, 0, 0, 1});  // leer
  };
  CHECK(os::run(direct("testgame", 12)) == 0);

  os::Profiles p;
  REQUIRE(p.load());
  REQUIRE(p.count() == 1);
  CHECK(std::string(p.at(0).name) == "BEN");
  CHECK(p.at(0).color == 3);
  const auto set = lastOf(9, WebPad::PROFILE_SET);
  REQUIRE(set.size() == 2);
  CHECK(set[1] == p.at(0).id);
  const auto rej = lastOf(9, WebPad::REJECT);
  REQUIRE(rej.size() == 2);
  CHECK(rej[1] == WebPad::REJECT_PROFILE);
  const auto pl = lastOf(9, WebPad::PROFILES);  // Liste nach dem Anlegen neu verschickt
  REQUIRE(pl.size() >= 2);
  CHECK(pl[1] == 1);
}

TEST_CASE("WebPad: falsche Version, Nachricht ohne HELLO, zweiter Tab") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testhal::onPoll = [](int f) {
    if (f == 2) {
      testhal::netOpen(1);
      testhal::netMsg(1, hello(0, os::kGuest, 99));
      testhal::netOpen(2);
      testhal::netMsg(2, state(A));
      testhal::netOpen(3);
      testhal::netMsg(3, hello(7, os::kGuest));
    }
    if (f == 4) { testhal::netOpen(4); testhal::netMsg(4, hello(7, os::kGuest)); }
  };
  CHECK(os::run(direct("testgame", 8)) == 0);
  const auto r1 = lastOf(1, WebPad::REJECT);
  REQUIRE(r1.size() == 2);
  CHECK(r1[1] == WebPad::REJECT_VERSION);
  CHECK(lastOf(2, WebPad::RESYNC).size() == 1);
  const auto r3 = lastOf(3, WebPad::REJECT);  // gleiches Token in Verbindung 4: 3 ist raus
  REQUIRE(r3.size() == 2);
  CHECK(r3[1] == WebPad::REJECT_REPLACED);
  CHECK(lastOf(4, WebPad::REJECT).empty());
}

TEST_CASE("WebPad: Netz startet nicht, Einstellung bleibt") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testhal::netStartOk = false;
  CHECK(os::run(direct("testgame", 5)) == 0);
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(s.webpadOn == 1);
  CHECK(std::strlen(s.apPass) == 8);  // Zugangsdaten beim ersten Start angelegt
  CHECK(std::strlen(s.apSuffix) == 4);
}

TEST_CASE("WebPad: Geräte-ID aus dem Token, Rumble-Weiterleitung") {
  uint8_t tok[16] = {};
  const uint32_t id = WebPad::idFromToken(tok);
  CHECK(WebPad::isWebPadId(id));
  CHECK_FALSE(WebPad::isWebPadId(1));            // Tastatur
  CHECK_FALSE(WebPad::isWebPadId(0x80001234u));  // SDL-Gamepad
  tok[3] = 1;
  CHECK(WebPad::idFromToken(tok) != id);
  CHECK(os::webpadRumble(id, 100));    // gehört dem WebPad, auch ohne Verbindung
  CHECK_FALSE(os::webpadRumble(1, 100));
}

TEST_CASE("Einstellungen: Version 2 wird übernommen, Sprache Englisch") {
  testhal::reset();
  testhal::clearStorage();
  os::Settings v2;
  v2.version = 2;
  v2.brightnessLevel = 2;
  v2.webpadOn = 1;
  std::memcpy(v2.apSuffix, "BEEF", 5);
  std::memcpy(v2.apPass, "12345678", 9);
  REQUIRE(hal::storage::save("os", "settings", &v2, offsetof(os::Settings, lang)));
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(s.version == 3);
  CHECK(s.brightnessLevel == 2);
  CHECK(s.webpadOn == 1);
  CHECK(std::string(s.apPass) == "12345678");
  CHECK(s.lang == uint8_t(Lang::En));
}

TEST_CASE("Sprache: Systemmenü schaltet um, gespeichert, Handys bekommen WELCOME (SPEC 12.4)") {
  testhal::reset();
  testhal::clearStorage();
  enableWebPad();
  testgame::stats = {};
  testhal::onPoll = [](int f) {
    uint16_t b = 0;
    if (f == 2) {
      testhal::netOpen(5);
      testhal::netMsg(5, hello(10, os::kGuest));
    }
    if (f > 2 && f % 10 == 0) testhal::netMsg(5, state(0));  // Heartbeat
    if (f >= 10 && f < 75) b = uint16_t(Button::Start) | uint16_t(Button::Select);  // Menü auf
    // WEITER, BEENDEN, SPIELER, PROFILE, REKORDE, WEBPAD, HELL, ENGLISH
    if (f >= 78 && f <= 90 && f % 2 == 0) b = uint16_t(Button::Down);
    if (f == 94) b = uint16_t(Button::Right);  // -> DEUTSCH
    if (f == 96) CHECK(os::g_lang == Lang::De);
    if (f == 98) b = uint16_t(Button::B);      // schließen, speichert
    testhal::device.state.buttons = b;
  };
  CHECK(os::run(direct("testgame", 140)) == 0);
  const auto w = lastOf(5, WebPad::WELCOME);
  REQUIRE(w.size() == 4 + 2 * os::kProfileColors);
  CHECK(w.back() == uint8_t(Lang::De));
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(s.lang == uint8_t(Lang::De));
  os::Profiles p;
  CHECK(std::string(p.name(os::kGuest)) == "GAST");

  // nächster Start übernimmt die Sprache aus den Einstellungen
  os::g_lang = Lang::En;
  testhal::reset();
  testhal::onPoll = nullptr;
  CHECK(os::run(direct("testgame", 5)) == 0);
  CHECK(os::g_lang == Lang::De);
  os::g_lang = Lang::En;  // andere Tests erwarten die Vorgabe
}

TEST_CASE("Einstellungen: Version 1 wird übernommen") {
  testhal::reset();
  testhal::clearStorage();
  struct V1 { uint8_t version, brightnessLevel; char lastGame[13]; } v1{1, 3, "snake"};
  REQUIRE(hal::storage::save("os", "settings", &v1, sizeof v1));
  os::Settings s;
  REQUIRE(os::loadSettings(s));
  CHECK(s.version == 3);
  CHECK(s.brightnessLevel == 3);
  CHECK(std::string(s.lastGame) == "snake");
  CHECK(s.webpadOn == 0);
  CHECK(s.apPass[0] == 0);
}
