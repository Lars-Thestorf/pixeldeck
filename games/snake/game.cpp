// SNAKE: 1-4 Spieler. Schlangen fressen Futter und wachsen; Wand oder ein
// Körper beenden das Leben. D-Pad oder linker Stick lenken.
//
// Modi (Auswahl vor dem Start, wird gespeichert):
// - KLASSIK: nur Futter. Allein zählt der Highscore.
// - POWER:   zusätzlich farbige Powerups mit zeitbegrenzter Wirkung:
//            blau = langsamer, gelb = schneller, weiß = unverwundbar
//            (durch Körper hindurch, Wände führen auf die Gegenseite),
//            violett = unsichtbar (nur der Kopf ist zu sehen).
// Ab 2 Spielern gewinnt, wer als Letzter übrig bleibt; Siege werden gezählt.
#include <cstdio>
#include <cstring>

#include "pd/pd.h"
#include "snake_assets.h"

using namespace pd;

namespace {

constexpr int CELL = 2;
constexpr int GW = Gfx::W / CELL;          // 32
constexpr int TOP = 6;                     // Platz für die Punktezeile
constexpr int GH = (Gfx::H - TOP) / CELL;  // 13
constexpr int MAXLEN = GW * GH;
constexpr int NP = Input::MAX_PLAYERS;
constexpr int MAX_ITEMS = 4;

constexpr uint16_t FIELD = rgb(0, 18, 6);
constexpr uint16_t HEAD = rgb(140, 255, 140);
constexpr uint16_t FOOD = rgb(255, 50, 60);

// Powerups. Reihenfolge = Effekt-Index; Item-Typ 0 ist Futter, 1..4 die Powerups.
enum Fx : uint8_t { Slow, Fast, Ghost, Invisible, FX_COUNT };
constexpr uint16_t FX_COLOR[FX_COUNT] = {rgb(40, 80, 255), rgb(255, 220, 0),
                                         rgb(255, 255, 255), rgb(170, 60, 255)};
constexpr const char* FX_NAME[FX_COUNT] = {"LANGSAM", "SCHNELL", "UNVERWUNDBAR", "UNSICHTBAR"};
constexpr uint16_t FX_TICKS[FX_COUNT] = {360, 360, 300, 360};  // Wirkdauer bei 60 Hz
constexpr uint16_t ITEM_TTL = 600;                            // Powerup liegt 10 s
constexpr int SPAWN_MIN = 180, SPAWN_RAND = 240;              // alle 3-7 s ein neues
constexpr int MAX_POWERUPS = 2;
constexpr int COUNTDOWN = 150;
constexpr int DEATH_TICKS = 60;  // so lange blinkt ein toter Körper und bleibt Hindernis

// Schlangenfarben im Mehrspieler nach Slot (wie Tennis feste Farben, damit zwei
// Gäste unterscheidbar bleiben).
struct Rgb { uint8_t r, g, b; };
constexpr Rgb SNAKE_RGB[NP] = {{40, 220, 60}, {255, 120, 0}, {0, 190, 255}, {255, 70, 170}};

constexpr Sfx kEat{Wave::Square, 600, 900, 60, 180, 64, 0, 20};
constexpr Sfx kPower{Wave::Square, 400, 1200, 120, 180, 96, 0, 30};
constexpr Sfx kDie{Wave::Noise, 400, 80, 400, 220, 0, 0, 150};

enum class Mode : uint8_t { Classic, Power };

struct Save {
  uint8_t mode;
};

class Snake : public Game {
public:
  void init(Context& ctx) override {
    Save s{};
    if (ctx.storage.load(&s, sizeof s) && s.mode <= uint8_t(Mode::Power)) mode_ = Mode(s.mode);
    savedMode_ = mode_;
    enterMenu(ctx);
  }

  void onShutdown(Context& ctx) override {
    if (mode_ == savedMode_) return;
    const Save s{uint8_t(mode_)};
    ctx.storage.save(&s, sizeof s);
  }

  void tick(Context& ctx) override {
    Input& in = ctx.input;
    ++timer_;
    switch (state_) {
      case State::Menu:
        if (in.pressed(Button::Left) || in.pressed(Button::Right) || in.pressed(Button::Up) ||
            in.pressed(Button::Down)) {
          mode_ = mode_ == Mode::Classic ? Mode::Power : Mode::Classic;
          timer_ = 0;
        }
        if (playersChanged(ctx)) newRound(ctx);
        if (in.pressed(Button::A)) {
          newRound(ctx);
          state_ = State::Ready;
        }
        break;
      case State::Ready:
        if (playersChanged(ctx)) newRound(ctx);
        for (int i = 0; i < NP; ++i)
          if (s_[i].active) readDir(in.player(i), s_[i]);
        if (in.pressed(Button::B)) {
          enterMenu(ctx);
        } else if (multi_ ? timer_ >= COUNTDOWN
                          : (in.pressed(Button::A) || s_[0].queued > 0)) {
          state_ = State::Run;
        }
        break;
      case State::Run:
        run(ctx);
        break;
      case State::Over:
        if (timer_ > 40) {
          if (in.pressed(Button::B)) {
            enterMenu(ctx);
          } else {
            for (int i = 0; i < NP; ++i)
              if (in.player(i).connected() && in.player(i).pressed(Button::A)) {
                newRound(ctx);
                state_ = State::Ready;
                break;
              }
          }
        }
        break;
    }
    draw(ctx);
  }

private:
  enum class State : uint8_t { Menu, Ready, Run, Over };
  enum Dir : uint8_t { Up, Right, Down, Left };

  struct Body {
    uint8_t xs[MAXLEN], ys[MAXLEN];  // Ringpuffer der Segmente
    int head, len;
    uint8_t dir, queue[2], queued;
    bool active, alive;
    uint8_t moveTimer, deathTimer;
    uint16_t fx[FX_COUNT];  // verbleibende Ticks je Effekt
    int32_t score;

    int seg(int i) const { return (head - i + MAXLEN) % MAXLEN; }  // 0 = Kopf
    bool has(Fx f) const { return fx[f] > 0; }
  };

  struct Item {
    int8_t x, y;    // x < 0: leer
    uint8_t type;   // 0 = Futter, sonst Fx + 1
    uint16_t ttl;
  };

  // --- Runden ----------------------------------------------------------------

  void enterMenu(Context& ctx) {
    std::memset(wins_, 0, sizeof wins_);
    newRound(ctx);
    state_ = State::Menu;
  }

  uint8_t connectedMask(Context& ctx) const {
    uint8_t m = 1;  // Slot 0 spielt immer
    for (int i = 1; i < NP; ++i)
      if (ctx.input.player(i).connected()) m |= uint8_t(1 << i);
    return m;
  }

  bool playersChanged(Context& ctx) const { return connectedMask(ctx) != mask_; }

  void newRound(Context& ctx) {
    std::memset(occ_, 0, sizeof occ_);
    std::memset(s_, 0, sizeof s_);
    for (Item& it : items_) it.x = -1;
    mask_ = connectedMask(ctx);
    int n = 0;
    for (int i = 0; i < NP; ++i) n += (mask_ >> i) & 1;
    multi_ = n >= 2;

    // Startreihen gleichmäßig verteilt, abwechselnd von links und rechts, damit
    // niemand frontal auf einen anderen zufährt.
    int k = 0;
    for (int i = 0; i < NP; ++i) {
      if (!((mask_ >> i) & 1)) continue;
      Body& b = s_[i];
      b.active = b.alive = true;
      b.len = 4;
      b.head = b.len - 1;
      const int y = multi_ ? (k + 1) * GH / (n + 1) : GH / 2;
      const bool fromRight = multi_ && (k & 1);
      for (int j = 0; j < b.len; ++j) {
        b.xs[j] = uint8_t(fromRight ? GW - 3 - j : 2 + j + (multi_ ? 0 : 4));
        b.ys[j] = uint8_t(y);
        ++occ_[y][b.xs[j]];
      }
      b.dir = fromRight ? Left : Right;
      ++k;
    }
    for (int f = 0; f < (n >= 3 ? 2 : 1); ++f) spawnItem(ctx, 0, 0);
    spawnTimer_ = SPAWN_MIN + int(ctx.random(SPAWN_RAND));
    timer_ = 0;
    winner_ = -1;
  }

  // Bis zu zwei Richtungswechsel puffern, damit schnelle Kurven nicht verloren gehen.
  static void readDir(Player& p, Body& b) {
    static constexpr Button keys[4] = {Button::Up, Button::Right, Button::Down, Button::Left};
    for (uint8_t d = 0; d < 4; ++d) {
      if (!p.pressed(keys[d])) continue;
      const uint8_t last = b.queued ? b.queue[b.queued - 1] : b.dir;
      if (d == last || d == (last + 2) % 4 || b.queued >= 2) continue;
      b.queue[b.queued++] = d;
    }
  }

  static int interval(const Body& b) {
    int v = 8 - b.len / 8;  // schneller mit wachsender Länge
    if (v < 3) v = 3;
    if (b.has(Fast)) v = v * 2 / 3;
    if (b.has(Slow)) v = v * 3 / 2;
    return v < 2 ? 2 : v;
  }

  // --- Spielablauf -------------------------------------------------------------

  void run(Context& ctx) {
    // Tote Körper blinken noch kurz als Hindernis, dann verschwinden sie.
    for (Body& b : s_) {
      if (!b.active || b.alive || b.deathTimer == 0) continue;
      if (--b.deathTimer == 0) {
        for (int i = 0; i < b.len; ++i) {
          const int idx = b.seg(i);
          --occ_[b.ys[idx]][b.xs[idx]];
        }
      }
    }

    if (mode_ == Mode::Power) {
      for (Body& b : s_)
        if (b.alive)
          for (uint16_t& t : b.fx)
            if (t) --t;
      for (Item& it : items_)
        if (it.x >= 0 && it.type && --it.ttl == 0) it.x = -1;
      if (--spawnTimer_ <= 0) {
        spawnTimer_ = SPAWN_MIN + int(ctx.random(SPAWN_RAND));
        int count = 0;
        for (const Item& it : items_) count += it.x >= 0 && it.type;
        if (count < MAX_POWERUPS) spawnItem(ctx, uint8_t(1 + ctx.random(FX_COUNT)), ITEM_TTL);
      }
    }

    // Wer zieht in diesem Tick? Ziele zuerst sammeln, dann gemeinsam auflösen,
    // damit die Slot-Reihenfolge keinen Vorteil bringt.
    struct Move { bool go, dead, grow, tailFreed; int nx, ny, item; } mv[NP] = {};
    static constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    for (int i = 0; i < NP; ++i) {
      Body& b = s_[i];
      if (!b.alive) continue;
      readDir(ctx.input.player(i), b);
      if (++b.moveTimer < interval(b)) continue;
      b.moveTimer = 0;
      if (b.queued) {
        b.dir = b.queue[0];
        b.queue[0] = b.queue[1];
        --b.queued;
      }
      Move& m = mv[i];
      m.go = true;
      m.nx = b.xs[b.head] + dx[b.dir];
      m.ny = b.ys[b.head] + dy[b.dir];
      if (b.has(Ghost)) {
        m.nx = (m.nx + GW) % GW;
        m.ny = (m.ny + GH) % GH;
      } else if (m.nx < 0 || m.ny < 0 || m.nx >= GW || m.ny >= GH) {
        m.dead = true;
      }
      m.item = m.dead ? -1 : itemAt(m.nx, m.ny);
      m.grow = m.item >= 0 && items_[m.item].type == 0;
    }

    // Schwänze geben ihr Feld frei, bevor Köpfe es prüfen (außer beim Wachsen).
    for (int i = 0; i < NP; ++i) {
      Move& m = mv[i];
      if (!m.go || m.dead || m.grow) continue;
      Body& b = s_[i];
      const int t = b.seg(b.len - 1);
      --occ_[b.ys[t]][b.xs[t]];
      m.tailFreed = true;
    }

    for (int i = 0; i < NP; ++i) {
      Move& m = mv[i];
      if (!m.go || m.dead || s_[i].has(Ghost)) continue;
      if (occ_[m.ny][m.nx]) m.dead = true;
      for (int j = 0; j < NP; ++j)  // zwei Köpfe auf dasselbe Feld
        if (j != i && mv[j].go && !mv[j].dead && mv[j].nx == m.nx && mv[j].ny == m.ny)
          m.dead = true;
    }

    // Anwenden
    int eaten = 0;
    for (int i = 0; i < NP; ++i) {
      Move& m = mv[i];
      if (!m.go) continue;
      Body& b = s_[i];
      if (m.dead) {
        if (m.tailFreed) {
          const int t = b.seg(b.len - 1);
          ++occ_[b.ys[t]][b.xs[t]];
        }
        kill(ctx, i);
        continue;
      }
      b.head = (b.head + 1) % MAXLEN;
      b.xs[b.head] = uint8_t(m.nx);
      b.ys[b.head] = uint8_t(m.ny);
      ++occ_[m.ny][m.nx];
      if (m.item < 0 || items_[m.item].x < 0) continue;  // schon von einem Geist gefressen
      Item& it = items_[m.item];
      if (it.type == 0) {
        ++b.len;
        b.score += 1 + b.len / 10;
        ctx.audio.play(kEat);
        it.x = -1;
        ++eaten;
      } else {
        const Fx f = Fx(it.type - 1);
        b.fx[f] = FX_TICKS[f];
        if (f == Fast) b.fx[Slow] = 0;
        if (f == Slow) b.fx[Fast] = 0;
        b.score += 2;
        ctx.audio.play(kPower);
        ctx.input.player(i).rumble(60);
        it.x = -1;
      }
    }

    while (eaten--) spawnItem(ctx, 0, 0);  // erst nach allen Zügen, nicht unter einen Kopf

    // Rundenende
    int alive = 0, last = -1;
    for (int i = 0; i < NP; ++i)
      if (s_[i].alive) {
        ++alive;
        last = i;
      }
    const bool full = !multi_ && s_[0].len >= MAXLEN;  // Feld voll: gewonnen
    if ((multi_ && alive <= 1) || (!multi_ && alive == 0) || full) {
      state_ = State::Over;
      timer_ = 0;
      if (multi_) {
        winner_ = last;
        if (last >= 0) ++wins_[last];
      } else if (mode_ == Mode::Classic) {
        ctx.scores.submit(0, s_[0].score);  // Highscore nur allein im Klassik-Modus
      }
    }
  }

  void kill(Context& ctx, int i) {
    s_[i].alive = false;
    s_[i].deathTimer = DEATH_TICKS;
    ctx.audio.play(kDie);
    ctx.input.player(i).rumble(250);
  }

  int itemAt(int x, int y) const {
    for (int k = 0; k < MAX_ITEMS; ++k)
      if (items_[k].x == x && items_[k].y == y) return k;
    return -1;
  }

  // Legt ein Item auf ein zufälliges freies Feld (n-tes freies Feld abzählen).
  void spawnItem(Context& ctx, uint8_t type, uint16_t ttl) {
    int slot = -1;
    for (int k = 0; k < MAX_ITEMS && slot < 0; ++k)
      if (items_[k].x < 0) slot = k;
    if (slot < 0) return;
    int freeCells = 0;
    for (int y = 0; y < GH; ++y)
      for (int x = 0; x < GW; ++x) freeCells += !occ_[y][x] && itemAt(x, y) < 0;
    if (freeCells <= 0) return;
    int k = int(ctx.random(uint32_t(freeCells)));
    for (int y = 0; y < GH; ++y)
      for (int x = 0; x < GW; ++x)
        if (!occ_[y][x] && itemAt(x, y) < 0 && k-- == 0) {
          items_[slot] = Item{int8_t(x), int8_t(y), type, ttl};
          return;
        }
  }

  // --- Zeichnen ----------------------------------------------------------------

  static uint16_t scaled(Rgb c, int pct) {
    return rgb(uint8_t(c.r * pct / 100), uint8_t(c.g * pct / 100), uint8_t(c.b * pct / 100));
  }

  uint16_t snakeColor(int slot, int i, int len) const {
    if (!multi_) {
      if (i == 0) return HEAD;
      return rgb(20, uint8_t(220 - (i * 120 / (len > 1 ? len : 1))), 40);
    }
    const Rgb c = SNAKE_RGB[slot];
    if (i == 0)  // Kopf heller
      return rgb(uint8_t((c.r + 255) / 2), uint8_t((c.g + 255) / 2), uint8_t((c.b + 255) / 2));
    return scaled(c, 100 - i * 55 / (len > 1 ? len : 1));
  }

  void drawSnake(Context& ctx, int slot) {
    Gfx& g = ctx.gfx;
    const Body& b = s_[slot];
    const bool dying = !b.alive && b.deathTimer > 0;
    const bool over1p = !multi_ && state_ == State::Over;
    if (!b.alive && !dying && !over1p) return;

    const uint32_t t = ctx.tick();
    const bool ghost = b.alive && b.has(Ghost);
    // Unverwundbar: weißes Blinken, gegen Ende langsamer als Warnung
    const bool ghostFlash = ghost && (b.fx[Ghost] > 90 ? (t / 4) % 2 : (t / 10) % 2);
    const bool hidden = b.alive && b.has(Invisible) && state_ == State::Run;

    for (int i = b.len - 1; i >= 0; --i) {
      if (hidden && i > 0) continue;
      const int idx = b.seg(i);
      uint16_t c = snakeColor(slot, i, b.len);
      if (ghostFlash) c = color::WHITE;
      if ((dying || over1p) && ((dying ? b.deathTimer : timer_) / 8) % 2 == 0) c = rgb(120, 0, 0);
      g.fillRect(b.xs[idx] * CELL, TOP + b.ys[idx] * CELL, CELL, CELL, c);
    }
  }

  void drawItems(Context& ctx) {
    Gfx& g = ctx.gfx;
    const uint32_t t = ctx.tick();
    for (const Item& it : items_) {
      if (it.x < 0) continue;
      uint16_t c;
      if (it.type == 0) {
        c = (t / 10) % 4 ? FOOD : rgb(110, 10, 20);
      } else {
        // Powerups pulsieren (aus/an), kurz vor dem Verschwinden schnell
        const bool on = it.ttl < 120 ? (t / 4) % 2 : (t / 8) % 4 != 0;
        if (!on) continue;
        c = FX_COLOR[it.type - 1];
      }
      g.fillRect(it.x * CELL, TOP + it.y * CELL, CELL, CELL, c);
    }
  }

  // Effektleiste in Zeile 5 unter der Punktezeile: Restzeit des Effekts, bei
  // mehreren Effekten im Wechsel.
  void drawFxBar(Context& ctx, const Body& b, int x, int w) {
    if (mode_ != Mode::Power || !b.alive) return;
    int list[FX_COUNT], n = 0;
    for (int f = 0; f < FX_COUNT; ++f)
      if (b.fx[f]) list[n++] = f;
    if (!n) return;
    const int f = list[(ctx.tick() / 30) % n];
    const int len = (b.fx[f] * w + FX_TICKS[f] - 1) / FX_TICKS[f];
    ctx.gfx.hline(x, 5, len, FX_COLOR[f]);
  }

  void drawHud(Context& ctx) {
    Gfx& g = ctx.gfx;
    char buf[16];
    if (!multi_) {
      std::snprintf(buf, sizeof buf, "%ld", long(s_[0].score));
      g.text(0, 0, buf, color::WHITE);
      std::snprintf(buf, sizeof buf, "L%d", s_[0].len);
      g.text(Gfx::W - g.textWidth(buf), 0, buf, color::GRAY);
      drawFxBar(ctx, s_[0], 0, Gfx::W);
      return;
    }
    int n = 0;
    for (const Body& b : s_) n += b.active;
    const int w = Gfx::W / n;
    int k = 0;
    for (int i = 0; i < NP; ++i) {
      const Body& b = s_[i];
      if (!b.active) continue;
      const int x = k * w;
      std::snprintf(buf, sizeof buf, "%ld", long(b.score));
      g.text(x + 1, 0, buf, b.alive ? scaled(SNAKE_RGB[i], 100) : rgb(60, 60, 60));
      drawFxBar(ctx, b, x + 1, w - 2);
      ++k;
    }
  }

  void centered(Gfx& g, int y, const char* s, uint16_t c, Font f = Font::Small) {
    g.text((Gfx::W - g.textWidth(s, f)) / 2, y, s, c, f);
  }

  void draw(Context& ctx) {
    Gfx& g = ctx.gfx;
    g.clear();
    g.fillRect(0, TOP, Gfx::W, GH * CELL, FIELD);
    if (state_ != State::Menu) {
      drawItems(ctx);
      for (int i = 0; i < NP; ++i)
        if (s_[i].active) drawSnake(ctx, i);
      drawHud(ctx);
    }

    switch (state_) {
      case State::Menu: {
        for (int i = 0; i < NP; ++i)
          if (s_[i].active) drawSnake(ctx, i);
        g.fillRect(4, 1, 56, 30, color::BLACK);
        g.rect(4, 1, 56, 30, color::GRAY);
        char buf[16];
        int n = 0;
        for (const Body& b : s_) n += b.active;
        std::snprintf(buf, sizeof buf, "%d SPIELER", n);
        centered(g, 3, buf, color::GRAY);
        centered(g, 10, mode_ == Mode::Classic ? "< KLASSIK >" : "< POWER >", color::YELLOW);
        if (mode_ == Mode::Power) {
          // Legende der Powerups im Wechsel
          const int f = int((timer_ / 60) % FX_COUNT);
          const int w = 3 + g.textWidth(FX_NAME[f]);
          const int x = (Gfx::W - w) / 2;
          g.fillRect(x, 17, 2, 2, FX_COLOR[f]);
          g.text(x + 4, 16, FX_NAME[f], color::GRAY);
        } else {
          centered(g, 16, n == 1 ? "HIGHSCORE" : "NUR FUTTER", color::GRAY);
        }
        centered(g, 23, "A = START", color::WHITE);
        break;
      }
      case State::Ready:
        if (multi_) {
          char buf[12];
          std::snprintf(buf, sizeof buf, "%d", 3 - timer_ * 3 / COUNTDOWN);
          g.fillRect(27, 12, 10, 11, color::BLACK);
          centered(g, 14, buf, color::YELLOW, Font::Large);
        } else {
          g.fillRect(14, 13, 37, 9, color::BLACK);
          g.text(16, 15, "A = START", color::YELLOW);
        }
        break;
      case State::Run:
        break;
      case State::Over:
        if (timer_ <= 40) break;
        if (!multi_) {
          g.fillRect(4, 7, 56, 24, color::BLACK);
          g.rect(4, 7, 56, 24, color::GRAY);
          centered(g, 9, "ENDE", color::RED, Font::Large);
          centered(g, 18, "A = NOCHMAL", color::GRAY);
          centered(g, 24, "B = MODUS", color::GRAY);
        } else {
          const uint16_t wc = winner_ >= 0 ? scaled(SNAKE_RGB[winner_], 100) : color::GRAY;
          g.fillRect(2, 4, 60, 28, color::BLACK);
          g.rect(2, 4, 60, 28, wc);
          if (winner_ >= 0) {
            char who[16];
            std::snprintf(who, sizeof who, "P%d %s", winner_ + 1, ctx.input.player(winner_).name());
            centered(g, 6, who, wc);
            centered(g, 12, "GEWINNT", wc);
          } else {
            centered(g, 9, "UNENTSCHIEDEN", color::GRAY);
          }
          // Siege je Spieler in seiner Farbe, z.B. "2:1:0"
          char buf[8];
          int w = 0, k = 0;
          for (int i = 0; i < NP; ++i)
            if (s_[i].active) {
              std::snprintf(buf, sizeof buf, "%d", wins_[i]);
              w += g.textWidth(buf) + (k++ ? 4 : 0);
            }
          int x = (Gfx::W - w) / 2;
          k = 0;
          for (int i = 0; i < NP; ++i) {
            if (!s_[i].active) continue;
            if (k++) x += g.text(x, 18, ":", color::GRAY) + 1;
            std::snprintf(buf, sizeof buf, "%d", wins_[i]);
            x += g.text(x, 18, buf, scaled(SNAKE_RGB[i], 100)) + 1;
          }
          centered(g, 25, "A=NEU B=MODUS", color::GRAY);
        }
        break;
    }
  }

  Body s_[NP];
  Item items_[MAX_ITEMS];
  uint8_t occ_[GH][GW];  // Anzahl Segmente je Feld (Geister dürfen überlappen)
  uint8_t wins_[NP] = {};
  uint8_t mask_ = 1;
  bool multi_ = false;
  int spawnTimer_ = 0;
  int timer_ = 0;
  int winner_ = -1;
  Mode mode_ = Mode::Classic, savedMode_ = Mode::Classic;
  State state_ = State::Menu;
};

}  // namespace

PD_REGISTER_GAME(Snake, {
  .id          = "snake",
  .name        = "SNAKE",
  .icon        = assets::icon,
  .scoreMode   = ScoreMode::Higher,
  .minPlayers  = 1,
  .maxPlayers  = 4,
  .saveVersion = 1,
  .stickAsDpad = true,
})
