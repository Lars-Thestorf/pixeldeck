// SNAKE: 1 Spieler, Highscore. Schlange frisst Futter und wächst; Wand oder
// eigener Körper beenden die Runde. D-Pad oder linker Stick lenken.
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

constexpr uint16_t FIELD = rgb(0, 18, 6);
constexpr uint16_t HEAD = rgb(140, 255, 140);
constexpr uint16_t FOOD = rgb(255, 50, 60);

constexpr Sfx kEat{Wave::Square, 600, 900, 60, 180, 64, 0, 20};
constexpr Sfx kDie{Wave::Noise, 400, 80, 400, 220, 0, 0, 150};

class Snake : public Game {
public:
  void init(Context& ctx) override { reset(ctx); }

  void tick(Context& ctx) override {
    Input& in = ctx.input;
    switch (state_) {
      case State::Ready:
        readDir(in);
        if (in.pressed(Button::A) || queued_ > 0) state_ = State::Run;
        break;
      case State::Run:
        readDir(in);
        if (++timer_ >= interval()) {
          timer_ = 0;
          step(ctx);
        }
        break;
      case State::Over:
        ++timer_;
        if (timer_ > 40 && in.pressed(Button::A)) reset(ctx);
        break;
    }
    draw(ctx);
  }

private:
  enum class State : uint8_t { Ready, Run, Over };
  enum Dir : uint8_t { Up, Right, Down, Left };

  void reset(Context& ctx) {
    std::memset(occ_, 0, sizeof occ_);
    len_ = 4;
    head_ = len_ - 1;
    for (int i = 0; i < len_; ++i) {
      xs_[i] = uint8_t(6 + i);
      ys_[i] = uint8_t(GH / 2);
      occ_[ys_[i]][xs_[i]] = 1;
    }
    dir_ = Right;
    queued_ = 0;
    score_ = 0;
    timer_ = 0;
    state_ = State::Ready;
    placeFood(ctx);
  }

  // Bis zu zwei Richtungswechsel puffern, damit schnelle Kurven nicht verloren gehen.
  void readDir(Input& in) {
    static constexpr Button keys[4] = {Button::Up, Button::Right, Button::Down, Button::Left};
    for (uint8_t d = 0; d < 4; ++d) {
      if (!in.pressed(keys[d])) continue;
      const uint8_t last = queued_ ? queue_[queued_ - 1] : dir_;
      if (d == last || d == (last + 2) % 4 || queued_ >= 2) continue;
      queue_[queued_++] = d;
    }
  }

  int interval() const {
    const int v = 8 - len_ / 8;  // schneller mit wachsender Länge
    return v < 3 ? 3 : v;
  }

  void step(Context& ctx) {
    if (queued_) {
      dir_ = queue_[0];
      queue_[0] = queue_[1];
      --queued_;
    }
    static constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    const int nx = xs_[head_] + dx[dir_], ny = ys_[head_] + dy[dir_];
    const bool eat = nx == foodX_ && ny == foodY_;

    // Schwanz gibt sein Feld frei, bevor der Kopf es prüft (außer beim Wachsen)
    const int tail = (head_ - len_ + 1 + MAXLEN) % MAXLEN;
    if (!eat) occ_[ys_[tail]][xs_[tail]] = 0;

    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || occ_[ny][nx]) {
      if (!eat) occ_[ys_[tail]][xs_[tail]] = 1;
      state_ = State::Over;
      timer_ = 0;
      ctx.audio.play(kDie);
      ctx.input.player(0).rumble(250);
      ctx.scores.submit(0, score_);
      return;
    }
    head_ = (head_ + 1) % MAXLEN;
    xs_[head_] = uint8_t(nx);
    ys_[head_] = uint8_t(ny);
    occ_[ny][nx] = 1;
    if (eat) {
      ++len_;
      score_ += 1 + len_ / 10;
      ctx.audio.play(kEat);
      if (len_ >= MAXLEN) {  // Feld voll: gewonnen
        state_ = State::Over;
        timer_ = 0;
        ctx.scores.submit(0, score_);
        return;
      }
      placeFood(ctx);
    }
  }

  void placeFood(Context& ctx) {
    // zufälliges freies Feld: n-tes freies Feld abzählen
    const int freeCells = MAXLEN - len_;
    if (freeCells <= 0) return;
    int k = int(ctx.random(uint32_t(freeCells)));
    for (int y = 0; y < GH; ++y)
      for (int x = 0; x < GW; ++x)
        if (!occ_[y][x] && k-- == 0) {
          foodX_ = x;
          foodY_ = y;
          return;
        }
  }

  void draw(Context& ctx) {
    Gfx& g = ctx.gfx;
    g.clear();
    g.fillRect(0, TOP, Gfx::W, GH * CELL, FIELD);

    // Körper mit leichtem Farbverlauf zum Schwanz
    for (int i = 0; i < len_; ++i) {
      const int idx = (head_ - i + MAXLEN) % MAXLEN;
      const uint8_t v = uint8_t(220 - (i * 120 / (len_ > 1 ? len_ : 1)));
      uint16_t c = i == 0 ? HEAD : rgb(20, v, 40);
      if (state_ == State::Over && (timer_ / 8) % 2 == 0) c = rgb(120, 0, 0);
      g.fillRect(xs_[idx] * CELL, TOP + ys_[idx] * CELL, CELL, CELL, c);
    }
    if (state_ != State::Over || len_ < MAXLEN)
      g.fillRect(foodX_ * CELL, TOP + foodY_ * CELL, CELL, CELL,
                 (ctx.tick() / 10) % 4 ? FOOD : rgb(255, 160, 160));

    char buf[16];
    std::snprintf(buf, sizeof buf, "%ld", long(score_));
    g.text(0, 0, buf, color::WHITE);
    std::snprintf(buf, sizeof buf, "L%d", len_);
    g.text(Gfx::W - g.textWidth(buf), 0, buf, color::GRAY);

    if (state_ == State::Ready) {
      g.fillRect(14, 13, 37, 9, color::BLACK);
      g.text(16, 15, "A = START", color::YELLOW);
    } else if (state_ == State::Over && timer_ > 40) {
      g.fillRect(7, 10, 50, 17, color::BLACK);
      g.rect(7, 10, 50, 17, color::GRAY);
      g.text(17, 12, "ENDE", color::RED, Font::Large);
      g.text(12, 20, "A = NOCHMAL", color::GRAY);
    }
  }

  uint8_t xs_[MAXLEN], ys_[MAXLEN];  // Ringpuffer der Segmente
  uint8_t occ_[GH][GW];
  int head_ = 0, len_ = 0;
  uint8_t dir_ = Right, queue_[2] = {}, queued_ = 0;
  int foodX_ = 0, foodY_ = 0;
  int timer_ = 0;
  int32_t score_ = 0;
  State state_ = State::Ready;
};

}  // namespace

PD_REGISTER_GAME(Snake, {
  .id          = "snake",
  .name        = "SNAKE",
  .icon        = assets::icon,
  .scoreMode   = ScoreMode::Higher,
  .minPlayers  = 1,
  .maxPlayers  = 1,
  .saveVersion = 1,
  .stickAsDpad = true,
})
