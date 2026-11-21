#include "pd/os/runtime.h"

#include <cstdarg>
#include <cstdio>

#include "arena.h"
#include "input_system.h"
#include "pcg32.h"
#include "pd/context.h"
#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/time.h"

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

}  // namespace

bool gameCodeActive() { return g_gameActive; }

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

class Runtime {
public:
  int run(const RunOptions& opts);

private:
  Gfx gfx_;
  Input input_;
  Audio audio_;
  Storage storage_;
  Scores scores_;
  Context ctx_{gfx_, input_, audio_, storage_, scores_};
  InputSystem inputSys_;
  Arena arena_{g_arenaMem, sizeof g_arenaMem};
};

int Runtime::run(const RunOptions& opts) {
  const GameInfo* info = nullptr;
  if (opts.gameId) {
    info = findGame(opts.gameId);
  } else if (gameCount() > 0) {
    info = &game(0);
    logf(LogLevel::Info, "Kein --game angegeben, starte '%s' (Launcher folgt in M2)",
         info->meta.id);
  }
  if (!info) {
    logf(LogLevel::Error, "Spiel '%s' nicht gefunden. Vorhanden:",
         opts.gameId ? opts.gameId : "");
    for (size_t i = 0; i < gameCount(); ++i)
      logf(LogLevel::Error, "  %s", game(i).meta.id);
    return 2;
  }
  const GameMeta& meta = info->meta;

  hal::display::init();

  // Spiel anlegen
  arena_.reset();
  arena_.resetPeak();
  void* mem = arena_.alloc(info->size, info->align);
  if (!mem) {
    logf(LogLevel::Error, "Spielobjekt '%s' passt nicht in die Arena", meta.id);
    return 3;
  }

  const uint32_t seed = opts.fixedSeed ? opts.seed : hal::random::seed();
  ctx_.tick_ = 0;
  ctx_.arena_ = &arena_;
  ctx_.gameId_ = meta.id;
  Pcg32::seed(ctx_.rngState_, ctx_.rngInc_, seed, kPcgStream);
  gfx_.clear();

  logf(LogLevel::Info, "Starte '%s' (seed %u)", meta.id, unsigned(seed));

  Game* g = nullptr;
  {
    GameCodeScope scope;
    g = info->create(mem);
    ctx_.allocAllowed_ = true;
    g->init(ctx_);
    ctx_.allocAllowed_ = false;
  }

  // Beim Start bereits gehaltene Tasten zählen nicht als "neu gedrückt".
  inputSys_.update(input_, meta.maxPlayers, meta.stickAsDpad);
  inputSys_.resetEdges(input_);

  const uint64_t period = 1000000u / PD_TICK_HZ;
  uint64_t next = hal::time::micros();
  uint64_t lastWarn = 0, sumUs = 0;
  uint32_t maxUs = 0, ticks = 0, slowTicks = 0;

  for (;;) {
    if (opts.maxTicks && ticks >= opts.maxTicks) break;

    inputSys_.update(input_, meta.maxPlayers, meta.stickAsDpad);
    if (hal::app::quitRequested()) break;
    if (inputSys_.menuComboTriggered()) {
      logf(LogLevel::Info, "Start+Select gehalten: Systemmenü folgt in M2, beende Spiel");
      break;
    }

    const uint64_t t0 = hal::time::micros();
    {
      GameCodeScope scope;
      g->tick(ctx_);
    }
    const uint64_t t1 = hal::time::micros();
    const uint32_t dt = uint32_t(t1 - t0);
    sumUs += dt;
    if (dt > maxUs) maxUs = dt;
    if (opts.tickWarnUs && dt > opts.tickWarnUs) {
      ++slowTicks;
      if (lastWarn == 0 || t1 - lastWarn > 1000000u) {
        logf(LogLevel::Warn, "tick() %u us (Grenze %u us, Tick %u)", unsigned(dt),
             unsigned(opts.tickWarnUs), unsigned(ctx_.tick_));
        lastWarn = t1;
      }
    }
    ++ctx_.tick_;
    ++ticks;

    hal::display::present(gfx_.buffer());

    // Fester Takt; zu langsame Ticks verlangsamen das Spiel, kein Frame-Skip.
    next += period;
    const uint64_t now = hal::time::micros();
    if (now >= next) next = now;
    else hal::time::sleepUntil(next);
  }

  {
    GameCodeScope scope;
    g->onShutdown(ctx_);
    g->~Game();
  }

  const size_t peak = arena_.peak(), cap = arena_.capacity();
  const unsigned pct = unsigned(peak * 100 / cap);
  logf(pct > 80 ? LogLevel::Warn : LogLevel::Info,
       "Arena-Höchststand '%s': %zu / %zu Bytes (%u %%)%s", meta.id, peak, cap, pct,
       pct > 80 ? " - über 80 %" : "");
  if (ticks > 0)
    logf(LogLevel::Info, "Ticks: %u, tick() Schnitt %u us, Maximum %u us%s", unsigned(ticks),
         unsigned(sumUs / ticks), unsigned(maxUs),
         slowTicks ? " (siehe Warnungen)" : "");
  arena_.reset();
  return 0;
}

int run(const RunOptions& opts) {
  static Runtime rt;  // 4 KB Framebuffer: nicht auf den Stack
  return rt.run(opts);
}

}  // namespace pd::os
