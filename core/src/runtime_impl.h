#pragma once
#include "arena.h"
#include "input_system.h"
#include "os_apps.h"
#include "pd/context.h"
#include "pd/os/runtime.h"
#include "settings.h"

namespace pd::os {

class Runtime {
public:
  int run(const RunOptions& opts);

  // Anforderungen der OS-Apps; ausgeführt nach dem laufenden Tick.
  void requestStart(const GameInfo* g) { pendingStart_ = g; }
  void requestEndGame() { pendingEnd_ = true; }
  void requestMenu(bool open) { pendingMenu_ = open ? 1 : 0; }

  bool inGame() const { return game_ != nullptr; }
  Settings& settings() { return settings_; }
  void setBrightnessLevel(int level);

private:
  void startGame(const GameInfo& info);
  void endGame();
  void openMenu();
  void closeMenu();
  void processRequests();
  void tickGame();

  RunOptions opts_;
  Gfx gfx_;
  Input input_;
  Audio audio_;
  Storage storage_;     // gebunden an das laufende Spiel
  Storage osStorage_;   // ungebunden, für OS-Apps
  Scores scores_;
  Context gameCtx_{gfx_, input_, audio_, storage_, scores_};
  Context osCtx_{gfx_, input_, audio_, osStorage_, scores_};
  InputSystem inputSys_;
  Arena arena_;
  Settings settings_;
  Launcher launcher_{*this};
  SystemMenu menu_{*this};

  const GameInfo* info_ = nullptr;
  Game* game_ = nullptr;
  bool menuOpen_ = false;

  const GameInfo* pendingStart_ = nullptr;
  bool pendingEnd_ = false;
  int pendingMenu_ = -1;

  // Statistik der aktuellen Spielsitzung
  uint64_t sumUs_ = 0, lastWarn_ = 0;
  uint32_t maxUs_ = 0, gameTicks_ = 0, slowTicks_ = 0;

public:
  Runtime();
};

}  // namespace pd::os
