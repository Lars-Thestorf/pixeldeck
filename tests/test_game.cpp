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
    uint32_t c = 0;
    testgame::stats.loaded = ctx.storage.load(&c, sizeof c);
    testgame::stats.counter = testgame::stats.loaded ? c : 0;
    uint64_t wrong = 0;
    testgame::stats.loadWrongSize = ctx.storage.load(&wrong, sizeof wrong);
  }
  void tick(pd::Context& ctx) override {
    if (testgame::stats.ticks == 0) testgame::stats.allocInTick = ctx.alloc(4) != nullptr;
    testgame::stats.lastTick = ctx.tick();
    ++testgame::stats.ticks;
    ctx.gfx.pixel(0, 0, 0x1234);
    if (ctx.input.player(1).connected()) testgame::stats.p2Seen = true;
    testgame::stats.p2Connected = ctx.input.player(1).connected();
    testgame::stats.p2Buttons = ctx.input.player(1).buttons();
    if (int(ctx.tick()) == testgame::stats.submitAtTick)
      ctx.scores.submit(testgame::stats.submitSlot, testgame::stats.submitValue);
  }
  void onShutdown(pd::Context& ctx) override {
    ++testgame::stats.shutdowns;
    const uint32_t c = testgame::stats.counter + 1;
    ctx.storage.save(&c, sizeof c);
  }
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
  .saveVersion = 3,
})
