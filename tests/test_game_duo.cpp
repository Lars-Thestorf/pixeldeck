// Zweites Testspiel: braucht mindestens 2 Spieler (Lobby, SPEC 6.4).
#include "pd/pd.h"
#include "test_game.h"

namespace testgame {
int duoTicks = 0;
}

namespace {

class Duo : public pd::Game {
public:
  void tick(pd::Context&) override { ++testgame::duoTicks; }
};

constexpr uint16_t kIconPx[256] = {};
constexpr pd::Sprite kIcon{16, 16, 1, kIconPx};

}  // namespace

PD_REGISTER_GAME(Duo, {
  .id = "duo",
  .name = "DUO",
  .icon = kIcon,
  .scoreMode = pd::ScoreMode::None,
  .minPlayers = 2,
  .maxPlayers = 2,
})
