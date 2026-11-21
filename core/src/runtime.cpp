#include "pd/os/runtime.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "pcg32.h"
#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/time.h"
#include "runtime_impl.h"

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
      startGame(*g);
      saveSettings(settings_);
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
  setBrightnessLevel(settings_.brightnessLevel);
  shuttingDown_ = false;
  noticeTicks_ = 0;
  hal::app::setActivity(nullptr);
  showResetNotice();

  osCtx_.tick_ = 0;
  Pcg32::seed(osCtx_.rngState_, osCtx_.rngInc_, hal::random::seed(), kPcgStream);
  if (direct) startGame(*direct);   // Direktstart überspringt den Launcher
  else launcher_.enter();

  // Beim Start bereits gehaltene Tasten zählen nicht als "neu gedrückt".
  inputSys_.update(input_, 1, true);
  inputSys_.resetEdges(input_);

  const uint64_t period = 1000000u / PD_TICK_HZ;
  uint64_t next = hal::time::micros();
  uint32_t ticks = 0;

  for (;;) {
    if (opts.maxTicks && ticks >= opts.maxTicks) break;

    const bool gameView = game_ && !menuOpen_;
    inputSys_.update(input_, gameView ? info_->meta.maxPlayers : 1,
                     gameView ? info_->meta.stickAsDpad : true);
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
    } else {
      launcher_.tick(osCtx_);
      ++osCtx_.tick_;
    }
    processRequests();
    ++ticks;

    hal::display::present(gfx_.buffer());

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
  saveSettings(settings_);
  return 0;
}

int run(const RunOptions& opts) {
  static Runtime rt;  // 4 KB Framebuffer: nicht auf den Stack
  return rt.run(opts);
}

}  // namespace pd::os
