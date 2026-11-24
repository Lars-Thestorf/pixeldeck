// TENNIS: Schläger-und-Ball-Spiel für 1-2 Spieler. Ohne zweiten Spieler
// übernimmt die CPU die rechte Seite. Hoch/Runter oder linker Stick (analog).
// Wer zuerst 7 Punkte hat, gewinnt.
#include <cmath>
#include <cstdio>

#include "pd/pd.h"
#include "tennis_assets.h"

using namespace pd;

namespace {

constexpr int WIN = 7;
constexpr int PH = 8;                      // Schlägerhöhe
constexpr float PADDLE_SPEED = 0.9f;
constexpr float CPU_SPEED = 0.62f;
constexpr float BALL_START = 0.75f, BALL_MAX = 2.0f;
constexpr int SERVE_DELAY = 50;

constexpr uint16_t COL_P[2] = {rgb(80, 200, 255), rgb(255, 150, 40)};
constexpr uint16_t NET = rgb(45, 45, 45);

constexpr Sfx kHit{Wave::Square, 520, 520, 40, 160, 128, 0, 10};
constexpr Sfx kWall{Wave::Triangle, 300, 300, 30, 140, 0, 0, 10};
constexpr Sfx kPoint{Wave::Square, 700, 200, 250, 180, 64, 0, 80};

class Tennis : public Game {
public:
  void init(Context& ctx) override { newMatch(ctx); }

  void tick(Context& ctx) override {
    const bool human2 = ctx.input.player(1).connected();
    if (state_ == State::Done) {
      if (++timer_ > 60 && ctx.input.pressed(Button::A)) newMatch(ctx);
      draw(ctx, human2);
      return;
    }

    movePaddle(ctx.input.player(0), py_[0]);
    if (human2) movePaddle(ctx.input.player(1), py_[1]);
    else cpu(ctx);

    if (state_ == State::Serve) {
      if (++timer_ >= SERVE_DELAY) launch(ctx);
    } else {
      moveBall(ctx);
    }
    draw(ctx, human2);
  }

private:
  enum class State : uint8_t { Serve, Play, Done };

  void newMatch(Context& ctx) {
    score_[0] = score_[1] = 0;
    py_[0] = py_[1] = (Gfx::H - PH) / 2.0f;
    serveTo_ = int(ctx.random(2));
    startServe();
  }

  void startServe() {
    state_ = State::Serve;
    timer_ = 0;
    bx_ = Gfx::W / 2.0f - 1;
    by_ = Gfx::H / 2.0f - 1;
    vx_ = vy_ = 0;
  }

  void launch(Context& ctx) {
    state_ = State::Play;
    speed_ = BALL_START;
    const float angle = (float(ctx.random(61)) - 30.0f) * 0.0174533f;  // +-30 Grad
    vx_ = std::cos(angle) * speed_ * (serveTo_ == 0 ? -1.0f : 1.0f);
    vy_ = std::sin(angle) * speed_;
  }

  static void movePaddle(Player& p, float& y) {
    float dy = 0;
    if (p.held(Button::Up)) dy -= PADDLE_SPEED;
    if (p.held(Button::Down)) dy += PADDLE_SPEED;
    if (dy == 0) dy = p.left().y * (PADDLE_SPEED * 1.3f) / 127.0f;
    y = clampY(y + dy);
  }

  static float clampY(float y) {
    if (y < 0) return 0;
    if (y > Gfx::H - PH) return float(Gfx::H - PH);
    return y;
  }

  // CPU folgt dem Ball nur, wenn er auf sie zukommt, mit begrenzter Geschwindigkeit
  // und einem zufälligen Zielversatz pro Ballwechsel.
  void cpu(Context& ctx) {
    float target = (Gfx::H - PH) / 2.0f;
    if (state_ == State::Play && vx_ > 0) target = by_ + 1 - PH / 2.0f + cpuOffset_;
    const float d = target - py_[1];
    py_[1] = clampY(py_[1] + (d > CPU_SPEED ? CPU_SPEED : (d < -CPU_SPEED ? -CPU_SPEED : d)));
    if ((ctx.tick() & 63) == 0) cpuOffset_ = float(int(ctx.random(7)) - 3);
  }

  void moveBall(Context& ctx) {
    bx_ += vx_;
    by_ += vy_;
    if (by_ < 0) { by_ = -by_; vy_ = -vy_; ctx.audio.play(kWall); }
    if (by_ > Gfx::H - 2) { by_ = 2.0f * (Gfx::H - 2) - by_; vy_ = -vy_; ctx.audio.play(kWall); }

    // Schläger: links x 1-2, rechts x 61-62; Ball 2x2
    if (vx_ < 0 && bx_ <= 3 && bx_ >= 0) bounce(ctx, 0);
    if (vx_ > 0 && bx_ + 2 >= 61 && bx_ + 2 <= 64) bounce(ctx, 1);

    if (bx_ < -3 || bx_ > Gfx::W + 1) {
      const int scorer = bx_ < 0 ? 1 : 0;
      ++score_[scorer];
      ctx.audio.play(kPoint);
      serveTo_ = 1 - scorer;  // Aufschlag zum Verlierer
      if (score_[scorer] >= WIN) {
        state_ = State::Done;
        timer_ = 0;
      } else {
        startServe();
      }
    }
  }

  void bounce(Context& ctx, int side) {
    const float rel = (by_ + 1) - (py_[side] + PH / 2.0f);  // -5..5
    if (rel < -PH / 2.0f - 1.5f || rel > PH / 2.0f + 1.5f) return;  // verfehlt
    speed_ = speed_ * 1.06f > BALL_MAX ? BALL_MAX : speed_ * 1.06f;
    const float angle = rel / (PH / 2.0f) * 0.9f;  // bis ca. 50 Grad
    vx_ = std::cos(angle) * speed_ * (side == 0 ? 1.0f : -1.0f);
    vy_ = std::sin(angle) * speed_;
    bx_ = side == 0 ? 3.0f : 59.0f;
    ctx.audio.play(kHit);
    ctx.input.player(side).rumble(40);
  }

  void draw(Context& ctx, bool human2) {
    Gfx& g = ctx.gfx;
    g.clear();
    for (int y = 0; y < Gfx::H; y += 4) g.vline(Gfx::W / 2, y, 2, NET);

    char buf[8];
    std::snprintf(buf, sizeof buf, "%d", score_[0]);
    g.text(Gfx::W / 2 - 3 - g.textWidth(buf, Font::Large), 1, buf, rgb(70, 70, 70), Font::Large);
    std::snprintf(buf, sizeof buf, "%d", score_[1]);
    g.text(Gfx::W / 2 + 4, 1, buf, rgb(70, 70, 70), Font::Large);

    g.fillRect(1, int(py_[0] + 0.5f), 2, PH, COL_P[0]);
    g.fillRect(61, int(py_[1] + 0.5f), 2, PH, COL_P[1]);

    if (state_ == State::Play || (state_ == State::Serve && (timer_ / 6) % 2 == 0))
      g.fillRect(int(bx_ + 0.5f), int(by_ + 0.5f), 2, 2, color::WHITE);

    if (state_ == State::Done) {
      const int w = score_[0] > score_[1] ? 0 : 1;
      const char* who = w == 0 ? ctx.tr("P1 WINS", "P1 GEWINNT")
                              : (human2 ? ctx.tr("P2 WINS", "P2 GEWINNT") : ctx.tr("CPU WINS", "CPU GEWINNT"));
      g.fillRect(4, 11, 56, 15, color::BLACK);
      g.rect(4, 11, 56, 15, COL_P[w]);
      g.text(32 - g.textWidth(who) / 2, 13, who, COL_P[w]);
      const char* again = ctx.tr("A = AGAIN", "A = NOCHMAL");
      if (timer_ > 60) g.text(32 - g.textWidth(again) / 2, 19, again, color::GRAY);
    }
  }

  float py_[2] = {};
  float bx_ = 0, by_ = 0, vx_ = 0, vy_ = 0, speed_ = BALL_START;
  float cpuOffset_ = 0;
  int score_[2] = {};
  int serveTo_ = 0, timer_ = 0;
  State state_ = State::Serve;
};

}  // namespace

PD_REGISTER_GAME(Tennis, {
  .id          = "tennis",
  .name        = "TENNIS",
  .icon        = assets::icon,
  .scoreMode   = ScoreMode::None,
  .minPlayers  = 1,
  .maxPlayers  = 2,
  .saveVersion = 1,
  .stickAsDpad = false,
})
