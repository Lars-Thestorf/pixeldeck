#pragma once
#include "arena.h"
#include "input_system.h"
#include "os_apps.h"
#include "pd/context.h"
#include "pd/hal/power.h"
#include "pd/os/runtime.h"
#include "profiles.h"
#include "settings.h"

namespace pd::os {

class Runtime {
public:
  int run(const RunOptions& opts);

  // Anforderungen der OS-Apps; ausgeführt nach dem laufenden Tick.
  void requestStart(const GameInfo* g) { pendingStart_ = g; }
  void requestEndGame() { pendingEnd_ = true; }
  void requestMenu(bool open) { pendingMenu_ = open ? 1 : 0; }
  void cancelLobby();

  bool inGame() const { return game_ != nullptr; }
  Settings& settings() { return settings_; }
  const hal::power::PowerState& power() const { return power_; }
  void setBrightnessLevel(int level);

  // Spieler, Profile, Highscores (SPEC 6.3, 7)
  InputSystem& inputSys() { return inputSys_; }
  Profiles& profiles() { return profiles_; }
  void saveProfiles();
  void setSlotProfile(int slot, uint8_t profileId);  // merkt es sich auch für das Gerät
  void deleteProfile(uint8_t id);
  void submitScore(int slot, int32_t score);
  const GameInfo* lobbyGame() const { return lobby_; }

private:
  void startGame(const GameInfo& info);
  void endGame();
  void openMenu();
  void closeMenu();
  void processRequests();
  void tickGame();
  void showResetNotice();
  bool tickNotice();
  void beginShutdown();
  void tickShutdown();
  void handleSlotEvents();
  void tickPicker();
  void showOverlay(uint16_t color, const char* fmt, int slot, const char* arg = nullptr);
  void presentFrame();

  RunOptions opts_;
  Gfx gfx_;
  Input input_;         // Sicht des Spiels (Slots)
  Input osInput_;       // Sicht der OS-Apps (Player 0 = Primärgerät)
  Audio audio_;
  Storage storage_;     // gebunden an das laufende Spiel
  Storage osStorage_;   // ungebunden, für OS-Apps
  Scores scores_;
  Scores osScores_;     // OS-Apps reichen keine Scores ein
  Context gameCtx_{gfx_, input_, audio_, storage_, scores_};
  Context osCtx_{gfx_, osInput_, audio_, osStorage_, osScores_};
  InputSystem inputSys_;
  Profiles profiles_;
  Arena arena_;
  Settings settings_;
  Launcher launcher_{*this};
  SystemMenu menu_{*this};
  Lobby lobbyApp_{*this};

  const GameInfo* info_ = nullptr;
  Game* game_ = nullptr;
  bool menuOpen_ = false;
  const GameInfo* lobby_ = nullptr;   // wartet auf Mitspieler (SPEC 6.4)

  const GameInfo* pendingStart_ = nullptr;
  bool pendingEnd_ = false;
  int pendingMenu_ = -1;

  hal::power::PowerState power_{};
  bool shuttingDown_ = false;
  uint32_t shutdownIdleTicks_ = 0;  // Ticks ohne shutdownPending während "BYE"

  // Hinweis nach Watchdog-Reset oder Absturz (SPEC 14.2)
  char notice_[3][17] = {};
  uint32_t noticeTicks_ = 0;

  // Profilwahl nach dem Join (SPEC 7.1), eine nach der anderen
  uint8_t pickQueue_ = 0;           // Slots, die noch wählen dürfen
  int     pickSlot_ = -1;
  int     pickSel_ = 0;
  bool    pickFresh_ = false;       // im Öffnungs-Tick keine Eingabe auswerten

  // Kurzmeldung oben (SPEC 12.3), Bildstreifen darunter wird nach present() zurückgeschrieben
  static constexpr int kOverlayH = 7;
  char     overlay_[17] = {};
  uint16_t overlayColor_ = 0;
  uint32_t overlayTicks_ = 0;
  uint16_t overlaySave_[Gfx::W * kOverlayH] = {};

  // Statistik der aktuellen Spielsitzung
  uint64_t sumUs_ = 0, lastWarn_ = 0;
  uint32_t maxUs_ = 0, gameTicks_ = 0, slowTicks_ = 0;

public:
  Runtime();
};

}  // namespace pd::os
