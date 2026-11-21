// Ersetzt die generierte game_registry.cpp für die Tests.
#include <cstddef>

#include "pd/registry.h"

const pd::GameInfo& pd_game_entry_testgame();
const pd::GameInfo& pd_game_entry_duo();

namespace pd::os::detail {
using GameEntry = const GameInfo& (*)();
extern const GameEntry kGames[];
extern const size_t kGameCount;
// testgame zuerst: der Launcher wählt ohne Einstellungen das erste Spiel
const GameEntry kGames[] = {&pd_game_entry_testgame, &pd_game_entry_duo};
const size_t kGameCount = 2;
}  // namespace pd::os::detail
