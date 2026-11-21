#pragma once

namespace pd {

class Context;

// Lebenszyklus (SPEC 4.1). Das Spielobjekt liegt am Anfang der Arena.
class Game {
public:
  virtual ~Game() = default;
  virtual void init(Context& ctx) { (void)ctx; }        // einmal; hier ctx.alloc() erlaubt
  virtual void tick(Context& ctx) = 0;                  // 60 Hz, Logik + Zeichnen
  virtual void onShutdown(Context& ctx) { (void)ctx; }  // vor Beenden; hier speichern
};

}  // namespace pd
