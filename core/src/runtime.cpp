#include "pd/os/runtime.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "os_call.h"
#include "pcg32.h"
#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/time.h"
#include "runtime_impl.h"
#include "score_table.h"

namespace pd::os {

namespace detail {
// Definiert in der von CMake generierten game_registry.cpp
using GameEntry = const GameInfo& (*)();
extern const GameEntry kGames[];
extern const size_t kGameCount;
}  // namespace detail

namespace {

// Arena im .bss (SPEC 9)
alignas(16) uint8_t g_arenaMem[PD_ARENA_SIZE];
bool g_gameActive = false;

struct GameCodeScope {
  GameCodeScope() { g_gameActive = true; }
  ~GameCodeScope() { g_gameActive = false; }
};

constexpr uint64_t kPcgStream = 0x9E3779B97F4A7C15ULL;

// Spielbild für das Menü abdunkeln (ca. 37 %)
void dim(Gfx& g) {
  uint16_t* p = g.buffer();
  for (int i = 0; i < Gfx::W * Gfx::H; ++i) {
    uint16_t c = p[i];
    unsigned r = (c >> 11) & 31, gr = (c >> 5) & 63, b = c & 31;
    p[i] = uint16_t(((r * 3 / 8) << 11) | ((gr * 3 / 8) << 5) | (b * 3 / 8));
  }
}

}  // namespace

bool gameCodeActive() { return g_gameActive; }

bool exchangeGameCodeActive(bool active) {
  const bool prev = g_gameActive;
  g_gameActive = active;
  return prev;
}

void logf(LogLevel level, const char* fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  hal::log::write(hal::log::Level(uint8_t(level)), msg);
}

size_t gameCount() { return detail::kGameCount; }
const GameInfo& game(size_t i) { return detail::kGames[i](); }

const GameInfo* findGame(const char* id) {
  if (!id) return nullptr;
  for (size_t i = 0; i < detail::kGameCount; ++i) {
    const GameInfo& g = detail::kGames[i]();
    if (pd::detail::streq(g.meta.id, id)) return &g;
  }
  return nullptr;
}

Runtime::Runtime() : arena_(g_arenaMem, sizeof g_arenaMem) {
  osCtx_.gameId_ = "os";
  scores_.rt_ = this;
  inputSys_.setProfiles(&profiles_);
}

void Runtime::cancelLobby() {
  lobby_ = nullptr;
  launcher_.enter();
}

void Runtime::saveProfiles() {
  if (!profiles_.save()) logf(LogLevel::Warn, "Profile konnten nicht gespeichert werden");
}

void Runtime::setSlotProfile(int slot, uint8_t profileId) {
  if (slot < 0 || slot >= InputSystem::SLOTS || !inputSys_.slot(slot).used) return;
  inputSys_.setSlotProfile(slot, profileId);
  const uint32_t dev = inputSys_.slot(slot).deviceId;
  if (inputSys_.slot(slot).type == hal::input::DeviceType::WebPad) {
    webpad_.profileSetByConsole(dev, profileId);  // das Handy merkt sich sein Profil selbst
    return;
  }
  profiles_.remember(dev, profileId);
  saveProfiles();
}

void Runtime::setWebPad(bool on) {
  if (on) webpad_.start();
  else webpad_.stop();
  settings_.webpadOn = webpad_.running() ? 1 : 0;
  saveSettings(settings_);
}

void Runtime::newWebPadPassword() {
  settings_.apPass[0] = 0;
  webpad_.ensureCredentials();
  if (webpad_.running()) {  // neues Passwort gilt erst nach Neustart des Access Points
    webpad_.stop();
    webpad_.start();
  }
}

void Runtime::deleteProfile(uint8_t id) {
  if (!profiles_.remove(id)) return;
  inputSys_.profileRemoved(id);
  saveProfiles();
}

// Highscore des Profils im Slot (SPEC 7.2). Aufruf aus dem Spiel über Scores::submit.
void Runtime::submitScore(int slot, int32_t score) {
  if (!info_) return;
  const GameMeta& m = info_->meta;
  if (!ScoreTable::counts(m.scoreMode, score)) return;
  uint8_t pid = kGuest;
  if (slot >= 0 && slot < InputSystem::SLOTS && inputSys_.slot(slot).used)
    pid = inputSys_.slot(slot).profileId;
  const char* name = profiles_.name(pid);
  static ScoreTable t;  // 164 Byte, nicht auf den Stack
  t.load(m.id);
  const int pos = t.insert(m.scoreMode, score, pid, name);
  if (pos < 0) return;
  if (!t.save(m.id)) logf(LogLevel::Warn, "Highscore '%s' nicht gespeichert", m.id);
  logf(LogLevel::Info, "Highscore '%s': Platz %d, %s %d", m.id, pos + 1, name, int(score));
}

void Runtime::setBrightnessLevel(int level) {
  if (level < 0) level = 0;
  if (level >= kBrightnessLevels) level = kBrightnessLevels - 1;
  settings_.brightnessLevel = uint8_t(level);
  hal::display::setBrightness(kBrightnessSteps[level]);
}

void Runtime::startGame(const GameInfo& info) {
  const GameMeta& meta = info.meta;
  arena_.reset();
  arena_.resetPeak();
  void* mem = arena_.alloc(info.size, info.align);
  if (!mem) {
    logf(LogLevel::Error, "Spielobjekt '%s' passt nicht in die Arena", meta.id);
    return;
  }

  std::snprintf(storage_.ns_, sizeof storage_.ns_, "g_%s", meta.id);
  storage_.version_ = meta.saveVersion;

  const uint32_t seed = opts_.fixedSeed ? opts_.seed : hal::random::seed();
  gameCtx_.tick_ = 0;
  gameCtx_.arena_ = &arena_;
  gameCtx_.gameId_ = meta.id;
  Pcg32::seed(gameCtx_.rngState_, gameCtx_.rngInc_, seed, kPcgStream);
  gfx_.clear();
  sumUs_ = lastWarn_ = 0;
  maxUs_ = gameTicks_ = slowTicks_ = 0;

  logf(LogLevel::Info, "Starte '%s' (seed %u)", meta.id, unsigned(seed));
  info_ = &info;
  lobby_ = nullptr;
  pickQueue_ = 0;
  pickSlot_ = -1;
  hal::app::setActivity(meta.id);  // ab hier gehen Hänger/Abstürze auf das Spiel
  {
    GameCodeScope scope;
    game_ = info.create(mem);
    gameCtx_.allocAllowed_ = true;
    game_->init(gameCtx_);
    gameCtx_.allocAllowed_ = false;
  }
  std::snprintf(settings_.lastGame, sizeof settings_.lastGame, "%s", meta.id);
}

void Runtime::endGame() {
  if (!game_) return;
  {
    GameCodeScope scope;
    game_->onShutdown(gameCtx_);
    game_->~Game();
  }
  game_ = nullptr;
  hal::app::setActivity(nullptr);
  const char* id = info_->meta.id;
  const size_t peak = arena_.peak(), cap = arena_.capacity();
  const unsigned pct = unsigned(peak * 100 / cap);
  logf(pct > 80 ? LogLevel::Warn : LogLevel::Info,
       "Arena-Höchststand '%s': %zu / %zu Bytes (%u %%)%s", id, peak, cap, pct,
       pct > 80 ? " - über 80 %" : "");
  if (gameTicks_ > 0)
    logf(LogLevel::Info, "Ticks '%s': %u, tick() Schnitt %u us, Maximum %u us%s", id,
         unsigned(gameTicks_), unsigned(sumUs_ / gameTicks_), unsigned(maxUs_),
         slowTicks_ ? " (siehe Warnungen)" : "");
  arena_.reset();
  storage_.ns_[0] = 0;
  info_ = nullptr;
  menuOpen_ = false;
  launcher_.enter();
}

void Runtime::openMenu() {
  if (menuOpen_) return;
  menuOpen_ = true;
  dim(gfx_);
  menu_.enter(game_ != nullptr);
}

void Runtime::closeMenu() {
  if (!menuOpen_) return;
  menuOpen_ = false;
  saveSettings(settings_);
}

// --- Hinweis nach Watchdog-Reset oder Absturz (SPEC 14.2, 12.3) ---------------

void Runtime::showResetNotice() {
  const hal::app::LastReset r = hal::app::lastReset();
  if (r.cause == hal::app::ResetCause::None) return;
  char id[sizeof r.gameId];
  std::memcpy(id, r.gameId, sizeof id);
  id[sizeof id - 1] = 0;
  const GameInfo* g = id[0] ? findGame(id) : nullptr;
  const char* name = g ? g->meta.name : (id[0] ? id : "SYSTEM");
  std::snprintf(notice_[0], sizeof notice_[0], "%s", name);
  for (char* c = notice_[0]; *c; ++c)
    if (*c >= 'a' && *c <= 'z') *c = char(*c - 'a' + 'A');
  if (r.cause == hal::app::ResetCause::Hang) {
    std::snprintf(notice_[1], sizeof notice_[1], "HAT NICHT");
    std::snprintf(notice_[2], sizeof notice_[2], "REAGIERT");
  } else {
    std::snprintf(notice_[1], sizeof notice_[1], "ABGESTUERZT");
    notice_[2][0] = 0;
  }
  noticeTicks_ = 5 * PD_TICK_HZ;
  logf(LogLevel::Warn, "Letzter Neustart: %s %s %s", notice_[0], notice_[1], notice_[2]);
}

// true, solange der Hinweis angezeigt wird. A, B oder Start schließt ihn.
bool Runtime::tickNotice() {
  if (noticeTicks_ == 0) return false;
  Player& p = input_.player(0);
  if (p.pressed(Button::A) || p.pressed(Button::B) || p.pressed(Button::Start)) {
    noticeTicks_ = 0;
    gfx_.clear();
    return true;  // Tastendruck gehört noch zum Hinweis
  }
  --noticeTicks_;
  gfx_.clear();
  constexpr uint16_t kRed = rgb(255, 60, 40);
  int y = notice_[2][0] ? 7 : 10;
  for (const char* line : notice_) {
    if (!line[0]) continue;
    gfx_.text((Gfx::W - gfx_.textWidth(line)) / 2, y, line, line == notice_[0] ? color::WHITE : kRed);
    y += 7;
  }
  return true;
}

// --- Abschalten (SPEC 14.3) -----------------------------------------------------

void Runtime::beginShutdown() {
  logf(LogLevel::Info, "Abschalten angefordert");
  shuttingDown_ = true;
  shutdownIdleTicks_ = 0;
  menuOpen_ = false;
  noticeTicks_ = 0;
  overlayTicks_ = 0;
  lobby_ = nullptr;
  pickQueue_ = 0;
  pickSlot_ = -1;
  if (game_) endGame();  // ruft onShutdown() des Spiels
  saveSettings(settings_);
  gfx_.clear();
  gfx_.text((Gfx::W - gfx_.textWidth("BYE", Font::Large)) / 2, 12, "BYE", color::WHITE, Font::Large);
  hal::power::shutdownReady();
}

// Wartet auf das Abschalten. Kommt es nicht (Taste nur kurz, Strom bleibt),
// geht es nach 3 s zurück in den Launcher.
void Runtime::tickShutdown() {
  if (power_.shutdownPending) shutdownIdleTicks_ = 0;
  else if (++shutdownIdleTicks_ >= 3u * PD_TICK_HZ) {
    logf(LogLevel::Info, "Abschalten ist ausgeblieben, zurück zum Launcher");
    shuttingDown_ = false;
    launcher_.enter();
  }
}

// --- Slots: Join, Trennung, Profilwahl (SPEC 6.3, 7.1, 12.3) -----------------------

void Runtime::showOverlay(uint16_t color, const char* fmt, int slot, const char* arg) {
  std::snprintf(overlay_, sizeof overlay_, fmt, slot + 1, arg ? arg : "");
  overlayColor_ = color;
  overlayTicks_ = 2 * PD_TICK_HZ;
}

void Runtime::handleSlotEvents() {
  const uint8_t joined = inputSys_.joinedMask(), lost = inputSys_.lostMask(),
                back = inputSys_.returnedMask(), expired = inputSys_.expiredMask();
  if (joined | lost | back | expired) {
    const int prim = inputSys_.primarySlot();
    for (int s = 0; s < InputSystem::SLOTS; ++s) {
      const uint8_t bit = uint8_t(1u << s);
      const InputSystem::Slot& sl = inputSys_.slot(s);
      if (joined & bit) {
        logf(LogLevel::Info, "P%d: %s beigetreten", s + 1, inputSys_.deviceName(sl.deviceId));
        if (sl.type == hal::input::DeviceType::WebPad) {
          // Profil kommt vom Handy (SPEC 7.1), keine Auswahl auf der Matrix
          const uint8_t want = webpad_.requestedProfile(sl.deviceId);
          if (!inputSys_.profileInUse(want, s)) inputSys_.setSlotProfile(s, want);
          showOverlay(profiles_.color(sl.profileId), "P%d %s", s, profiles_.name(sl.profileId));
        } else if (s != prim) {
          // im Spiel ohne Auswahl mit dem letzten Profil des Geräts, sonst Profilwahl
          if (game_) showOverlay(profiles_.color(sl.profileId), "P%d %s", s, profiles_.name(sl.profileId));
          else pickQueue_ |= bit;
        }
      }
      if (lost & bit) {
        logf(LogLevel::Info, "P%d getrennt, Slot reserviert", s + 1);
        showOverlay(rgb(255, 60, 40), "P%d GETRENNT", s);
      }
      if (back & bit) {
        logf(LogLevel::Info, "P%d wieder verbunden", s + 1);
        showOverlay(rgb(60, 220, 60), "P%d ZURUECK", s);
      }
      if (expired & bit) logf(LogLevel::Info, "P%d freigegeben", s + 1);
      if ((lost | expired) & bit) {
        pickQueue_ &= uint8_t(~bit);
        if (pickSlot_ == s) pickSlot_ = -1;
      }
    }
  }

  if (pickSlot_ < 0 && pickQueue_ && !game_ && !menuOpen_ && !shuttingDown_) {
    int s = 0;
    while (!(pickQueue_ & (1u << s))) ++s;
    pickQueue_ &= uint8_t(~(1u << s));
    const InputSystem::Slot& sl = inputSys_.slot(s);
    if (sl.used && sl.present) {
      pickSlot_ = s;
      pickSel_ = 0;
      pickFresh_ = true;
      // vorgewählt: aktuelles Profil des Slots (= zuletzt genutztes des Geräts)
      int idx = 0;
      for (int i = 0; i < profiles_.count(); ++i) {
        const uint8_t id = profiles_.at(i).id;
        if (inputSys_.profileInUse(id, s)) continue;
        ++idx;
        if (id == sl.profileId) pickSel_ = idx;
      }
    }
  }
}

// Profilwahl nach dem Join am eingebauten Pad, Gamepad oder an der Tastatur (SPEC 7.1).
// Bedient vom beitretenden Gerät; B (dort oder am Primärgerät) bricht den Join ab.
void Runtime::tickPicker() {
  const int s = pickSlot_;
  const InputSystem::Slot& sl = inputSys_.slot(s);
  uint8_t ids[1 + Profiles::MAX];
  int n = 0;
  ids[n++] = kGuest;
  for (int i = 0; i < profiles_.count(); ++i)
    if (!inputSys_.profileInUse(profiles_.at(i).id, s)) ids[n++] = profiles_.at(i).id;
  if (pickSel_ >= n) pickSel_ = 0;

  const Player* p = inputSys_.devicePlayer(sl.deviceId);
  if (!pickFresh_ && p) {
    if (p->pressed(Button::Left) || p->pressed(Button::Up)) pickSel_ = (pickSel_ + n - 1) % n;
    if (p->pressed(Button::Right) || p->pressed(Button::Down)) pickSel_ = (pickSel_ + 1) % n;
    if (p->pressed(Button::A)) {
      setSlotProfile(s, ids[pickSel_]);
      logf(LogLevel::Info, "P%d spielt als %s", s + 1, profiles_.name(ids[pickSel_]));
      pickSlot_ = -1;
    } else if (p->pressed(Button::B) || osInput_.player(0).pressed(Button::B)) {
      logf(LogLevel::Info, "P%d: Beitritt abgebrochen", s + 1);
      inputSys_.removeSlot(s);
      pickSlot_ = -1;
    }
  }
  pickFresh_ = false;

  Gfx& g = gfx_;
  g.clear();
  char buf[32];
  std::snprintf(buf, sizeof buf, "P%d: WER BIST DU?", s + 1);
  g.text((Gfx::W - g.textWidth(buf)) / 2, 0, buf, color::WHITE);
  const uint8_t id = ids[pickSel_ < n ? pickSel_ : 0];
  const char* name = profiles_.name(id);
  const uint16_t c = profiles_.color(id);
  if (g.textWidth(name, Font::Large) <= 54) g.text((Gfx::W - g.textWidth(name, Font::Large)) / 2, 11, name, c, Font::Large);
  else g.text((Gfx::W - g.textWidth(name)) / 2, 12, name, c);
  if (n > 1) {
    g.text(0, 12, "<", color::GRAY);
    g.text(61, 12, ">", color::GRAY);
  }
  const char* dev = inputSys_.deviceName(sl.deviceId);
  g.text((Gfx::W - g.textWidth(dev)) / 2, 21, dev, color::GRAY);
  g.text((Gfx::W - g.textWidth("A OK  B ZURUECK")) / 2, 27, "A OK  B ZURUECK", rgb(60, 60, 60));
}

// Kurzmeldung oben einblenden; der überdeckte Streifen wird danach zurückgeschrieben,
// damit Spiele ohne clear() keine Reste sehen.
void Runtime::presentFrame() {
  if (overlayTicks_ == 0) {
    hal::display::present(gfx_.buffer());
    return;
  }
  --overlayTicks_;
  uint16_t* fb = gfx_.buffer();
  std::memcpy(overlaySave_, fb, sizeof overlaySave_);
  gfx_.fillRect(0, 0, Gfx::W, kOverlayH, color::BLACK);
  gfx_.text((Gfx::W - gfx_.textWidth(overlay_)) / 2, 1, overlay_, overlayColor_);
  hal::display::present(fb);
  std::memcpy(fb, overlaySave_, sizeof overlaySave_);
}

void Runtime::processRequests() {
  if (pendingEnd_) {
    pendingEnd_ = false;
    endGame();
    saveSettings(settings_);
  }
  if (pendingMenu_ >= 0) {
    if (pendingMenu_) openMenu();
    else closeMenu();
    pendingMenu_ = -1;
  }
  if (pendingStart_) {
    const GameInfo* g = pendingStart_;
    pendingStart_ = nullptr;
    if (!game_) {
      const GameMeta& m = g->meta;
      if (inputSys_.presentCount(m.maxPlayers) < m.minPlayers) {
        if (lobby_ != g) {
          logf(LogLevel::Info, "Lobby '%s': warte auf %u Spieler", m.id, unsigned(m.minPlayers));
          lobby_ = g;
          lobbyApp_.enter();
        }
      } else {
        startGame(*g);
        saveSettings(settings_);
      }
    }
  }
}

void Runtime::tickGame() {
  const uint64_t t0 = hal::time::micros();
  {
    GameCodeScope scope;
    game_->tick(gameCtx_);
  }
  const uint64_t t1 = hal::time::micros();
  const uint32_t dt = uint32_t(t1 - t0);
  sumUs_ += dt;
  if (dt > maxUs_) maxUs_ = dt;
  if (opts_.tickWarnUs && dt > opts_.tickWarnUs) {
    ++slowTicks_;
    if (lastWarn_ == 0 || t1 - lastWarn_ > 1000000u) {
      logf(LogLevel::Warn, "tick() %u us (Grenze %u us, Tick %u)", unsigned(dt),
           unsigned(opts_.tickWarnUs), unsigned(gameCtx_.tick_));
      lastWarn_ = t1;
    }
  }
  ++gameCtx_.tick_;
  ++gameTicks_;
}

int Runtime::run(const RunOptions& opts) {
  opts_ = opts;
  pendingStart_ = nullptr;
  pendingEnd_ = false;
  pendingMenu_ = -1;
  menuOpen_ = false;

  const GameInfo* direct = nullptr;
  if (opts.gameId) {
    direct = findGame(opts.gameId);
    if (!direct) {
      logf(LogLevel::Error, "Spiel '%s' nicht gefunden. Vorhanden:", opts.gameId);
      for (size_t i = 0; i < gameCount(); ++i) logf(LogLevel::Error, "  %s", game(i).meta.id);
      return 2;
    }
  }

  hal::display::init();
  settings_ = Settings{};
  loadSettings(settings_);
  profiles_.load();
  inputSys_.reset();
  lobby_ = nullptr;
  pickQueue_ = 0;
  pickSlot_ = -1;
  overlayTicks_ = 0;
  setBrightnessLevel(settings_.brightnessLevel);
  webpad_.reset();
  if (settings_.webpadOn || opts.webpad) webpad_.start();  // scheitert es, bleibt die Einstellung
  shuttingDown_ = false;
  noticeTicks_ = 0;
  hal::app::setActivity(nullptr);
  showResetNotice();

  osCtx_.tick_ = 0;
  Pcg32::seed(osCtx_.rngState_, osCtx_.rngInc_, hal::random::seed(), kPcgStream);

  // Beim Start bereits gehaltene Tasten zählen nicht als "neu gedrückt".
  inputSys_.update(input_, osInput_, InputSystem::SLOTS, true, false);
  inputSys_.resetEdges(input_, osInput_);

  launcher_.enter();
  if (direct) {                     // Direktstart überspringt den Launcher (ggf. Lobby)
    requestStart(direct);
    processRequests();
  }

  const uint64_t period = 1000000u / PD_TICK_HZ;
  uint64_t next = hal::time::micros();
  uint32_t ticks = 0;

  for (;;) {
    if (opts.maxTicks && ticks >= opts.maxTicks) break;

    const bool gameView = game_ && !menuOpen_;
    webpad_.update(ticks);
    const size_t nWeb = webpad_.devices(webpadDevs_, WebPad::MAX_CLIENTS);
    inputSys_.update(input_, osInput_, gameView ? info_->meta.maxPlayers : InputSystem::SLOTS,
                     gameView ? info_->meta.stickAsDpad : true, !shuttingDown_, webpadDevs_, nWeb);
    handleSlotEvents();
    webpad_.sync();
    if (hal::app::quitRequested()) break;
    hal::app::heartbeat();
    power_ = hal::power::read();
    if (power_.shutdownPending && !shuttingDown_) beginShutdown();

    if (shuttingDown_) {
      tickShutdown();
    } else if (tickNotice()) {
      // Hinweis nach Neustart verdeckt Launcher bzw. Direktstart
    } else if (inputSys_.menuComboTriggered()) {
      if (menuOpen_) closeMenu();
      else openMenu();
    } else if (menuOpen_) {
      menu_.tick(osCtx_);
      ++osCtx_.tick_;
    } else if (game_) {
      tickGame();
    } else if (pickSlot_ >= 0) {
      tickPicker();
      ++osCtx_.tick_;
    } else if (lobby_) {
      lobbyApp_.tick(osCtx_);
      ++osCtx_.tick_;
    } else {
      launcher_.tick(osCtx_);
      ++osCtx_.tick_;
    }
    processRequests();
    ++ticks;

    presentFrame();

    // Fester Takt; zu langsame Ticks verlangsamen das Spiel, kein Frame-Skip.
    next += period;
    const uint64_t now = hal::time::micros();
    if (now >= next) next = now;
    else hal::time::sleepUntil(next);
  }

  if (game_) {
    menuOpen_ = false;
    endGame();
  }
  webpad_.stop();
  saveSettings(settings_);
  return 0;
}

}  // namespace pd::os

void pd::Scores::submit(int slot, int32_t score) {
  if (!rt_) return;
  os::OsCallScope osCall;  // Speichern gehört dem OS (Heap-Hook am PC)
  rt_->submitScore(slot, score);
}

namespace pd::os {

int run(const RunOptions& opts) {
  static Runtime rt;  // 4 KB Framebuffer: nicht auf den Stack
  return rt.run(opts);
}

}  // namespace pd::os
