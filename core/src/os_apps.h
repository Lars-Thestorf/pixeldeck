#pragma once
#include <cstdint>

#include "pd/game.h"
#include "pd/registry.h"
#include "profiles.h"

namespace pd::os {

class Runtime;

// Launcher, Lobby und Systemmenü sind intern Spiele über dieselbe API (SPEC 12).
// Sie liegen statisch im OS, nicht in der Arena, und stehen nicht in der Registry.

class Launcher : public Game {
public:
  explicit Launcher(Runtime& rt) : rt_(rt) {}
  void enter();  // beim Wechsel in den Launcher: Auswahl auf zuletzt gespieltes Spiel
  void tick(Context& ctx) override;

private:
  void loadRecord();

  Runtime& rt_;
  int sel_ = 0;
  int slide_ = 0;  // Versatz der Blätter-Animation in Pixeln
  int recFor_ = -1;  // Spiel, zu dem rec* gehört
  bool recValid_ = false;
  int32_t recScore_ = 0;
  char recName_[kNameLen + 1] = {};
};

// Wartet, bis genug Spieler für minPlayers verbunden sind (SPEC 6.4).
class Lobby : public Game {
public:
  explicit Lobby(Runtime& rt) : rt_(rt) {}
  void enter() {}
  void tick(Context& ctx) override;

private:
  Runtime& rt_;
};

class SystemMenu : public Game {
public:
  explicit SystemMenu(Runtime& rt) : rt_(rt) {}
  void enter(bool inGame);
  void tick(Context& ctx) override;

  enum class Item : uint8_t { Resume, Quit, Players, Profiles, Scores, WebPad, Brightness, Back };
  enum class Page : uint8_t { Main, Players, Profiles, Edit, Scores, WebPad, WebPadQr };

private:
  void tickMain(Context& ctx);
  void tickPlayers(Context& ctx);
  void tickProfiles(Context& ctx);
  void tickEdit(Context& ctx);
  void tickScores(Context& ctx);
  void tickWebPad(Context& ctx);
  void tickWebPadQr(Context& ctx);
  void encodeQr();
  void openEdit(int profileIndex);  // -1 = neues Profil
  void loadScores();

  Runtime& rt_;
  Page page_ = Page::Main;
  Item items_[8] = {};
  int count_ = 0;
  int sel_ = 0;

  int  plSel_ = 0, plSwap_ = -1;      // Spieler-Seite: Auswahl, zum Tausch markierter Slot
  int  prSel_ = 0;                    // Profil-Liste
  int  edIndex_ = -1, edCursor_ = 0;  // Editor: Profilindex (-1 neu), Cursor
  uint8_t edColor_ = 0;
  char edName_[kNameLen + 1] = {};
  bool edConfirm_ = false;            // Löschen bestätigen
  int  scGame_ = 0, scTop_ = 0;       // Highscores: Spiel (Index in der Registry), Scroll
  bool scConfirm_ = false;
  bool scLoaded_ = false;
  int  qrStep_ = 0;                   // QR-Ansicht: 0 WLAN, 1 Seite
  int  qrLevel_ = 0;                  // QR-Ansicht: Helligkeitsstufe nur für diese Ansicht
  bool pwConfirm_ = false;            // WebPad: neues Passwort bestätigen
  bool qrOk_ = false;
};

}  // namespace pd::os
