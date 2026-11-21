// Testspiel für die Laufzeit-Tests (PD_GAME_SYMBOL=testgame per CMake)
#include "pd/pd.h"
#include "test_game.h"

namespace testgame {
Stats stats;
}

namespace {

class TestGame : public pd::Game {
public:
  void init(pd::Context& ctx) override {
    ++testgame::stats.inits;
    testgame::stats.allocInInit = ctx.alloc(100) != nullptr;
    testgame::stats.allocTooBig = ctx.alloc(PD_ARENA_SIZE) != nullptr;
    testgame::stats.firstRandom = ctx.random();
  }
  void tick(pd::Context& ctx) override {
    if (testgame::stats.ticks == 0) testgame::stats.allocInTick = ctx.alloc(4) != nullptr;
    testgame::stats.lastTick = ctx.tick();
    ++testgame::stats.ticks;
    ctx.gfx.pixel(0, 0, 0x1234);
  }
  void onShutdown(pd::Context&) override { ++testgame::stats.shutdowns; }
  ~TestGame() override { ++testgame::stats.destroyed; }
};

constexpr uint16_t kIconPx[256] = {};
constexpr pd::Sprite kIcon{16, 16, 1, kIconPx};

}  // namespace

PD_REGISTER_GAME(TestGame, {
  .id = "testgame",
  .name = "TEST",
  .icon = kIcon,
  .scoreMode = pd::ScoreMode::Higher,
  .minPlayers = 1,
  .maxPlayers = 2,
})
