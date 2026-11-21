// Ersetzt die generierte game_registry.cpp für die Tests.
#include <cstddef>

#include "pd/registry.h"

const pd::GameInfo& pd_game_entry_testgame();

namespace pd::os::detail {
using GameEntry = const GameInfo& (*)();
extern const GameEntry kGames[];
extern const size_t kGameCount;
const GameEntry kGames[] = {&pd_game_entry_testgame};
const size_t kGameCount = 1;
}  // namespace pd::os::detail
