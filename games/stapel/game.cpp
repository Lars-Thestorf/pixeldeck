// STAPEL: Fallblock-Spiel, 1 Spieler, Highscore, Spielstand.
// Zweierpaare aus farbigen Blöcken fallen in einen Schacht. Drei oder mehr
// gleiche Farben in einer Reihe (waagerecht oder senkrecht) lösen sich auf,
// darüberliegende Blöcke fallen nach; Kettenreaktionen zählen mehrfach.
// Links/Rechts schieben, A/B drehen, Runter schneller, Hoch fallen lassen.
#include <cstdio>
#include <cstring>

#include "pd/pd.h"
#include "stapel_assets.h"

using namespace pd;

namespace {

constexpr int COLS = 6, ROWS = 10, CELL = 3;
constexpr int FX = 2, FY = 1;  // Feldursprung in Pixeln
constexpr int NCOLORS = 4;
constexpr int HUD = 24;

constexpr uint16_t kColors[NCOLORS + 1] = {0, rgb(255, 60, 80), rgb(255, 210, 0),
                                           rgb(40, 200, 60), rgb(40, 110, 255)};
constexpr uint16_t BORDER = rgb(70, 70, 70);
constexpr uint16_t LABEL = rgb(110, 110, 110);

constexpr Sfx kMove{Wave::Square, 300, 300, 15, 90, 128, 0, 5};
constexpr Sfx kLand{Wave::Triangle, 180, 120, 50, 160, 0, 0, 20};
constexpr Sfx kClear{Wave::Square, 500, 1000, 180, 180, 64, 0, 60};
constexpr Sfx kOver{Wave::Noise, 300, 60, 600, 200, 0, 0, 200};

constexpr int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};  // Lage des 2. Blocks

// Spielstand (SPEC 8): reines POD, saveVersion in den Metadaten
struct SaveData {
  uint8_t field[ROWS][COLS];
  uint8_t cur[2], next[2];
  int8_t px, py, rot;
  uint8_t phase;  // 0 = Paar fällt, 1 = Feld setzt sich
  int32_t score;
  uint16_t cleared;
};

uint16_t brighten(uint16_t c) {
  unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  r = r + 10 > 31 ? 31 : r + 10;
  g = g + 20 > 63 ? 63 : g + 20;
  b = b + 10 > 31 ? 31 : b + 10;
  return uint16_t((r << 11) | (g << 5) | b);
}

void drawCell(Gfx& g, int x, int y, uint16_t c) {
  g.fillRect(x, y, CELL, CELL, c);
  g.pixel(x, y, brighten(c));
}

class Stapel : public Game {
public:
  void init(Context& ctx) override {
    SaveData sd;
    if (ctx.storage.load(&sd, sizeof sd) && restore(sd)) {
      state_ = State::Resume;
      ctx.log("Spielstand geladen (%ld Punkte)", long(score_));
    } else {
      newGame(ctx);
    }
  }

  void onShutdown(Context& ctx) override {
    if (state_ == State::Over || state_ == State::Ready) {
      ctx.storage.erase();
      return;
    }
    SaveData sd;
    std::memset(&sd, 0, sizeof sd);
    std::memcpy(sd.field, field_, sizeof field_);
    std::memcpy(sd.cur, cur_, 2);
    std::memcpy(sd.next, next_, 2);
    sd.px = int8_t(px_);
    sd.py = int8_t(py_);
    sd.rot = int8_t(rot_);
    sd.phase = (state_ == State::Fall || (state_ == State::Resume && resumePhase_ == 0)) ? 0 : 1;
    sd.score = score_;
    sd.cleared = uint16_t(cleared_);
    if (!ctx.storage.save(&sd, sizeof sd)) ctx.log("Spielstand konnte nicht gespeichert werden");
  }

  void tick(Context& ctx) override {
    Input& in = ctx.input;
    ++timer_;
    switch (state_) {
      case State::Ready:
        if (in.pressed(Button::A)) { state_ = State::Fall; timer_ = 0; }
        break;
      case State::Resume:
        if (in.pressed(Button::A)) { state_ = resumePhase_ == 0 ? State::Fall : State::Settle; timer_ = 0; }
        break;
      case State::Fall: fall(ctx); break;
      case State::Settle: settle(ctx); break;
      case State::Clear:
        if (timer_ >= 20) removeMarked(ctx);
        break;
      case State::Over:
        if (timer_ > 60 && in.pressed(Button::A)) {
          newGame(ctx);
          state_ = State::Fall;
        }
        break;
    }
    draw(ctx);
  }

private:
  enum class State : uint8_t { Ready, Resume, Fall, Settle, Clear, Over };

  void newGame(Context& ctx) {
    std::memset(field_, 0, sizeof field_);
    std::memset(mark_, 0, sizeof mark_);
    score_ = 0;
    cleared_ = 0;
    chain_ = 1;
    rollPair(ctx, next_);
    spawn(ctx);
    state_ = State::Ready;
    timer_ = 0;
  }

  bool restore(const SaveData& sd) {
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < COLS; ++c)
        if (sd.field[r][c] > NCOLORS) return false;
    for (int i = 0; i < 2; ++i)
      if (sd.cur[i] < 1 || sd.cur[i] > NCOLORS || sd.next[i] < 1 || sd.next[i] > NCOLORS)
        return false;
    if (sd.rot < 0 || sd.rot > 3 || sd.phase > 1) return false;
    std::memcpy(field_, sd.field, sizeof field_);
    std::memset(mark_, 0, sizeof mark_);
    std::memcpy(cur_, sd.cur, 2);
    std::memcpy(next_, sd.next, 2);
    px_ = sd.px;
    py_ = sd.py;
    rot_ = sd.rot;
    score_ = sd.score;
    cleared_ = sd.cleared;
    chain_ = 1;
    resumePhase_ = sd.phase;
    if (resumePhase_ == 0 && !fits(px_, py_, rot_)) return false;
    return true;
  }

  void rollPair(Context& ctx, uint8_t* p) {
    p[0] = uint8_t(1 + ctx.random(NCOLORS));
    p[1] = uint8_t(1 + ctx.random(NCOLORS));
  }

  int level() const { return 1 + cleared_ / 30; }
  int gravity() const {
    const int v = 36 - 3 * (level() - 1);
    return v < 5 ? 5 : v;
  }

  bool cellFree(int c, int r) const {
    if (c < 0 || c >= COLS || r >= ROWS) return false;
    return r < 0 || field_[r][c] == 0;  // über dem Schacht ist frei
  }

  bool fits(int x, int y, int rot) const {
    return cellFree(x, y) && cellFree(x + kDx[rot], y + kDy[rot]);
  }

  void spawn(Context& ctx) {
    cur_[0] = next_[0];
    cur_[1] = next_[1];
    rollPair(ctx, next_);
    px_ = 2;
    py_ = 0;
    rot_ = 0;
    fallTimer_ = 0;
    holdTimer_ = 0;
  }

  void fall(Context& ctx) {
    Input& in = ctx.input;
    // Schieben mit Wiederholung beim Halten
    int dir = 0;
    if (in.pressed(Button::Left)) { dir = -1; holdTimer_ = 0; }
    else if (in.pressed(Button::Right)) { dir = 1; holdTimer_ = 0; }
    else if (in.held(Button::Left) || in.held(Button::Right)) {
      if (++holdTimer_ >= 12 && (holdTimer_ - 12) % 4 == 0) dir = in.held(Button::Left) ? -1 : 1;
    }
    if (dir && fits(px_ + dir, py_, rot_)) { px_ += dir; ctx.audio.play(kMove); }

    // Drehen mit einfachem Ausweichen an Wänden und Boden
    int r = 0;
    if (in.pressed(Button::A)) r = 1;
    if (in.pressed(Button::B)) r = 3;
    if (r) {
      const int nr = (rot_ + r) % 4;
      static constexpr int kickX[4] = {0, -1, 1, 0}, kickY[4] = {0, 0, 0, -1};
      for (int k = 0; k < 4; ++k)
        if (fits(px_ + kickX[k], py_ + kickY[k], nr)) {
          px_ += kickX[k];
          py_ += kickY[k];
          rot_ = nr;
          ctx.audio.play(kMove);
          break;
        }
    }

    if (in.pressed(Button::Up)) {  // fallen lassen
      while (fits(px_, py_ + 1, rot_)) ++py_;
      lock(ctx);
      return;
    }
    const int interval = in.held(Button::Down) ? 2 : gravity();
    if (++fallTimer_ >= interval) {
      fallTimer_ = 0;
      if (fits(px_, py_ + 1, rot_)) {
        ++py_;
        if (in.held(Button::Down)) ++score_;
      } else {
        lock(ctx);
      }
    }
  }

  void lock(Context& ctx) {
    const int x2 = px_ + kDx[rot_], y2 = py_ + kDy[rot_];
    if (py_ < 0 || y2 < 0) {  // ragt über den Schacht: verloren
      gameOver(ctx);
      return;
    }
    field_[py_][px_] = cur_[0];
    field_[y2][x2] = cur_[1];
    ctx.audio.play(kLand);
    chain_ = 1;
    state_ = State::Settle;
    timer_ = 0;
  }

  // Freistehende Blöcke fallen zeilenweise nach; danach Reihen prüfen.
  void settle(Context& ctx) {
    if (timer_ % 3) return;
    bool moved = false;
    for (int r = ROWS - 2; r >= 0; --r)
      for (int c = 0; c < COLS; ++c)
        if (field_[r][c] && !field_[r + 1][c]) {
          field_[r + 1][c] = field_[r][c];
          field_[r][c] = 0;
          moved = true;
        }
    if (moved) return;
    if (findMatches()) {
      state_ = State::Clear;
      timer_ = 0;
      ctx.audio.play(kClear);
      ctx.input.player(0).rumble(uint16_t(60 * chain_));
      return;
    }
    spawn(ctx);
    if (!fits(px_, py_, rot_)) { gameOver(ctx); return; }
    state_ = State::Fall;
  }

  bool findMatches() {
    std::memset(mark_, 0, sizeof mark_);
    bool any = false;
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < COLS; ++c) {
        const uint8_t v = field_[r][c];
        if (!v) continue;
        int n = 1;  // waagerecht ab (c, r)
        while (c + n < COLS && field_[r][c + n] == v) ++n;
        if (n >= 3) { for (int i = 0; i < n; ++i) mark_[r][c + i] = 1; any = true; }
        n = 1;      // senkrecht ab (c, r)
        while (r + n < ROWS && field_[r + n][c] == v) ++n;
        if (n >= 3) { for (int i = 0; i < n; ++i) mark_[r + i][c] = 1; any = true; }
      }
    return any;
  }

  void removeMarked(Context& ctx) {
    int n = 0;
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < COLS; ++c)
        if (mark_[r][c]) { field_[r][c] = 0; mark_[r][c] = 0; ++n; }
    score_ += int32_t(n * 10 * chain_ * level());
    cleared_ += n;
    lastChain_ = chain_;
    chainShow_ = chain_ > 1 ? 60 : 0;
    ++chain_;
    state_ = State::Settle;
    timer_ = 0;
    (void)ctx;
  }

  void gameOver(Context& ctx) {
    state_ = State::Over;
    timer_ = 0;
    ctx.audio.play(kOver);
    ctx.input.player(0).rumble(300);
    ctx.scores.submit(0, score_);
    ctx.storage.erase();
  }

  void draw(Context& ctx) {
    Gfx& g = ctx.gfx;
    g.clear();
    g.rect(FX - 1, FY - 1, COLS * CELL + 2, ROWS * CELL + 2, BORDER);

    const bool flash = state_ == State::Clear && (timer_ / 3) % 2 == 0;
    for (int r = 0; r < ROWS; ++r)
      for (int c = 0; c < COLS; ++c) {
        if (!field_[r][c]) continue;
        uint16_t col = kColors[field_[r][c]];
        if (state_ == State::Over && timer_ < 60) col = (timer_ / 6) % 2 ? rgb(60, 60, 60) : col;
        drawCell(g, FX + c * CELL, FY + r * CELL, mark_[r][c] && flash ? color::WHITE : col);
      }

    const bool showPiece = state_ == State::Fall || state_ == State::Ready ||
                           (state_ == State::Resume && resumePhase_ == 0);
    if (showPiece) {
      const int xs[2] = {px_, px_ + kDx[rot_]}, ys[2] = {py_, py_ + kDy[rot_]};
      for (int i = 0; i < 2; ++i)
        if (ys[i] >= 0) drawCell(g, FX + xs[i] * CELL, FY + ys[i] * CELL, kColors[cur_[i]]);
    }

    // Vorschau des nächsten Paars
    g.rect(53, 0, 10, 7, BORDER);
    drawCell(g, 55, 2, kColors[next_[0]]);
    drawCell(g, 58, 2, kColors[next_[1]]);

    char buf[16];
    g.text(HUD, 1, ctx.tr("SCORE", "PUNKTE"), LABEL);
    std::snprintf(buf, sizeof buf, "%ld", long(score_));
    g.text(HUD, 7, buf, color::WHITE);
    std::snprintf(buf, sizeof buf, "LV %d", level());
    g.text(HUD, 14, buf, LABEL);

    if (chainShow_ > 0) {
      --chainShow_;
      std::snprintf(buf, sizeof buf, "%s x%d", ctx.tr("CHAIN", "KETTE"), lastChain_);
      g.text(HUD, 21, buf, color::YELLOW);
    }
    switch (state_) {
      case State::Ready: g.text(HUD, 27, "A = START", color::YELLOW); break;
      case State::Resume: g.text(HUD, 27, ctx.tr("A = RESUME", "A = WEITER"), color::YELLOW); break;
      case State::Over:
        g.text(HUD, 21, ctx.tr("GAME OVER", "ENDE"), color::RED);
        if (timer_ > 60) g.text(HUD, 27, ctx.tr("A = NEW", "A = NEU"), color::GRAY);
        break;
      default: break;
    }
  }

  uint8_t field_[ROWS][COLS] = {};
  uint8_t mark_[ROWS][COLS] = {};
  uint8_t cur_[2] = {1, 1}, next_[2] = {1, 1};
  int px_ = 2, py_ = 0, rot_ = 0;
  int fallTimer_ = 0, holdTimer_ = 0, timer_ = 0;
  int32_t score_ = 0;
  int cleared_ = 0, chain_ = 1, lastChain_ = 1, chainShow_ = 0;
  uint8_t resumePhase_ = 0;
  State state_ = State::Ready;
};

}  // namespace

PD_REGISTER_GAME(Stapel, {
  .id          = "stapel",
  .name        = "STACK",
  .icon        = assets::icon,
  .scoreMode   = ScoreMode::Higher,
  .minPlayers  = 1,
  .maxPlayers  = 1,
  .saveVersion = 1,
  .stickAsDpad = true,
  .nameDe      = "STAPEL",
})
