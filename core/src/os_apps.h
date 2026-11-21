#pragma once
#include <cstdint>

#include "pd/game.h"
#include "pd/registry.h"

namespace pd::os {

class Runtime;

// Launcher und Systemmenü sind intern Spiele über dieselbe API (SPEC 12).
// Sie liegen statisch im OS, nicht in der Arena, und stehen nicht in der Registry.

class Launcher : public Game {
public:
  explicit Launcher(Runtime& rt) : rt_(rt) {}
  void enter();  // beim Wechsel in den Launcher: Auswahl auf zuletzt gespieltes Spiel
  void tick(Context& ctx) override;

private:
  Runtime& rt_;
  int sel_ = 0;
  int slide_ = 0;  // Versatz der Blätter-Animation in Pixeln
};

class SystemMenu : public Game {
public:
  explicit SystemMenu(Runtime& rt) : rt_(rt) {}
  void enter(bool inGame);
  void tick(Context& ctx) override;

  enum class Item : uint8_t { Resume, Quit, Brightness, Back };

private:
  Runtime& rt_;
  Item items_[4] = {};
  int count_ = 0;
  int sel_ = 0;
};

}  // namespace pd::os
