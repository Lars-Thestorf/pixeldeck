// Testbild: Testspiel für M1. Prüft Eingabe, beide Fonts, Zeichenfunktionen,
// Sprites (Animation, Spiegeln), Arena (ctx.alloc) und PRNG.
// L/R wechselt die Seite.
#include <array>
#include <cmath>
#include <cstdio>

#include "pd/pd.h"

using namespace pd;

namespace {

// --- Assets (bis zum Asset-Konverter in M2 direkt im Code) ---------------------

constexpr uint16_t pal(char c) {
  switch (c) {
    case 'y': return rgb(255, 210, 0);
    case 'o': return rgb(230, 120, 0);
    case 'k': return rgb(0, 0, 0);
    case 'r': return rgb(255, 60, 80);
    case 'w': return rgb(255, 255, 255);
    default:  return TRANSPARENT;
  }
}

template <size_t N>
constexpr std::array<uint16_t, N> art(const char* s) {
  std::array<uint16_t, N> a{};
  for (size_t i = 0; i < N; ++i) a[i] = pal(s[i]);
  return a;
}

// 8x8, 2 Frames (Augen offen / zu)
constexpr auto kBlobData = art<128>(
    "..yyyy.."
    ".yyyyyy."
    "yywkywky"
    "yykkykky"
    "yyyyyyyy"
    "yryyyyry"
    ".oyrryo."
    "..oooo.."
    //
    "..yyyy.."
    ".yyyyyy."
    "yyyyyyyy"
    "yykkykky"
    "yyyyyyyy"
    "yryyyyry"
    ".oyrryo."
    "..oooo..");
constexpr Sprite kBlob{8, 8, 2, kBlobData.data()};

// 16x16 Icon: Farbbalken mit weißem Rahmen
constexpr std::array<uint16_t, 256> makeIcon() {
  constexpr uint16_t bars[7] = {color::WHITE, color::YELLOW, color::CYAN, color::GREEN,
                                rgb(255, 0, 200), color::RED, color::BLUE};
  std::array<uint16_t, 256> a{};
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      bool border = x == 0 || y == 0 || x == 15 || y == 15;
      a[size_t(y * 16 + x)] = border ? color::WHITE : (y > 11 ? rgb(40, 40, 40) : bars[(x - 1) / 2 % 7]);
    }
  return a;
}
constexpr auto kIconData = makeIcon();
constexpr Sprite kIcon{16, 16, 1, kIconData.data()};

constexpr uint16_t DIM = rgb(50, 50, 50);

// --- Spiel ---------------------------------------------------------------------

class Testbild : public Game {
public:
  void init(Context& ctx) override {
    parts_ = static_cast<Particle*>(ctx.alloc(sizeof(Particle) * NP, alignof(Particle)));
    if (parts_)
      for (int i = 0; i < NP; ++i) parts_[i].life = 0;
    ctx.log("bereit, L/R wechselt die Seite");
  }

  void tick(Context& ctx) override {
    Input& in = ctx.input;
    if (in.pressed(Button::R)) { page_ = (page_ + 1) % PAGES; scroll_ = 0; }
    if (in.pressed(Button::L)) { page_ = (page_ + PAGES - 1) % PAGES; scroll_ = 0; }

    Gfx& g = ctx.gfx;
    g.clear();
    switch (page_) {
      case 0: pageInput(ctx); break;
      case 1: pageFonts(ctx); break;
      case 2: pageShapes(ctx); break;
      default: pageSprite(ctx); break;
    }
    for (int i = 0; i < PAGES; ++i) g.pixel(Gfx::W - 2 * PAGES + 2 * i + 1, Gfx::H - 1, i == page_ ? color::WHITE : DIM);
  }

  void onShutdown(Context& ctx) override { ctx.log("beendet nach %u Ticks", unsigned(ctx.tick())); }

private:
  static constexpr int PAGES = 4;
  static constexpr int NP = 48;
  struct Particle { int16_t x, y, vx, vy; uint8_t life; uint16_t c; };  // 1/16 px

  // Seite 1: alle Buttons und Sticks
  void pageInput(Context& ctx) {
    Gfx& g = ctx.gfx;
    Player& p = ctx.input.player(0);
    auto btn = [&](Button b, int x, int y, int w, int h, uint16_t on) {
      g.fillRect(x, y, w, h, p.pressed(b) ? color::WHITE : (p.held(b) ? on : DIM));
    };
    btn(Button::L, 1, 0, 14, 2, color::ORANGE);
    btn(Button::R, 49, 0, 14, 2, color::ORANGE);
    btn(Button::Up, 7, 10, 3, 3, color::CYAN);
    btn(Button::Down, 7, 16, 3, 3, color::CYAN);
    btn(Button::Left, 4, 13, 3, 3, color::CYAN);
    btn(Button::Right, 10, 13, 3, 3, color::CYAN);
    btn(Button::X, 54, 9, 3, 3, color::BLUE);
    btn(Button::Y, 50, 13, 3, 3, color::GREEN);
    btn(Button::A, 58, 13, 3, 3, color::RED);
    btn(Button::B, 54, 17, 3, 3, color::YELLOW);
    btn(Button::Select, 26, 23, 5, 2, color::GRAY);
    btn(Button::Start, 33, 23, 5, 2, color::WHITE);

    auto stick = [&](Stick s, bool click, int cx, int cy) {
      if (click) g.fillCircle(cx, cy, 5, rgb(0, 80, 160));
      g.circle(cx, cy, 5, DIM);
      g.fillRect(cx + s.x * 4 / 127 - 1, cy + s.y * 4 / 127 - 1, 2, 2, color::WHITE);
    };
    stick(p.left(), p.held(Button::L3), 22, 14);
    stick(p.right(), p.held(Button::R3), 41, 14);

    char buf[12];
    std::snprintf(buf, sizeof buf, "%04X", unsigned(p.buttons()));
    g.text(1, 26, buf, p.connected() ? color::GRAY : color::RED);
  }

  // Seite 2: Zeichensatz, A wechselt den Font, Hoch/Runter blättert
  void pageFonts(Context& ctx) {
    Gfx& g = ctx.gfx;
    Input& in = ctx.input;
    if (in.pressed(Button::A)) { large_ = !large_; scroll_ = 0; }
    const Font f = large_ ? Font::Large : Font::Small;
    const FontMetrics m = fontMetrics(f);
    const int cols = Gfx::W / m.advance, rows = Gfx::H / m.lineHeight;
    const int totalRows = (95 + cols - 1) / cols;
    if (in.pressed(Button::Down) && scroll_ < totalRows - rows) ++scroll_;
    if (in.pressed(Button::Up) && scroll_ > 0) --scroll_;

    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < cols; ++c) {
        int ch = 32 + (scroll_ + r) * cols + c;
        if (ch > 126) break;
        char s[2] = {char(ch), 0};
        uint16_t col = rgb(uint8_t(120 + c * 8), uint8_t(255 - r * 30), 255);
        g.text(c * m.advance, r * m.lineHeight, s, col, f);
      }
  }

  // Seite 3: Linien, Kreise, Rechtecke, Farbverläufe
  void pageShapes(Context& ctx) {
    Gfx& g = ctx.gfx;
    const uint32_t t = ctx.tick();
    const float a = float(t) * 0.05f;
    const int cx = 12, cy = 12;
    g.circle(cx, cy, 11, DIM);
    g.line(cx, cy, cx + int(std::lround(10.0f * std::cos(a))),
           cy + int(std::lround(10.0f * std::sin(a))), color::GREEN);
    g.line(cx, cy, cx + int(std::lround(7.0f * std::cos(a * 0.25f))),
           cy + int(std::lround(7.0f * std::sin(a * 0.25f))), color::RED);

    const int r = int(t / 6 % 9);
    g.fillCircle(38, 11, r, rgb(0, 90, 200));
    g.circle(38, 11, 9, color::CYAN);
    g.rect(50, 2, 13, 19, color::YELLOW);
    g.fillRect(53 + int(t / 10 % 7), 5, 3, 13, color::PURPLE);
    // Clipping: Rechteck läuft über den Rand
    g.fillRect(int(t / 2 % 90) - 20, 22, 12, 3, color::ORANGE);

    for (int x = 0; x < Gfx::W; ++x) {
      uint8_t v = uint8_t(x * 4 + 3);
      g.pixel(x, 26, rgb(v, 0, 0));
      g.pixel(x, 27, rgb(0, v, 0));
      g.pixel(x, 28, rgb(0, 0, v));
      g.pixel(x, 29, rgb(v, v, v));
    }
  }

  // Seite 4: Sprite bewegen (D-Pad/linker Stick), A/B spiegeln, X Funken, Y Rumble
  void pageSprite(Context& ctx) {
    Gfx& g = ctx.gfx;
    Player& p = ctx.input.player(0);
    const int speed = 12;  // 1/16 px pro Tick
    if (p.held(Button::Left)) sx_ -= speed;
    if (p.held(Button::Right)) sx_ += speed;
    if (p.held(Button::Up)) sy_ -= speed;
    if (p.held(Button::Down)) sy_ += speed;
    sx_ += p.left().x * speed / 127;
    sy_ += p.left().y * speed / 127;
    sx_ = sx_ < -64 ? -64 : (sx_ > (Gfx::W - 4) * 16 ? (Gfx::W - 4) * 16 : sx_);
    sy_ = sy_ < -64 ? -64 : (sy_ > (Gfx::H - 4) * 16 ? (Gfx::H - 4) * 16 : sy_);
    if (p.pressed(Button::A)) flip_ ^= FLIP_X;
    if (p.pressed(Button::B)) flip_ ^= FLIP_Y;
    if (p.pressed(Button::Y)) p.rumble(150);
    if (p.held(Button::X) && parts_) spawn(ctx, sx_ + 4 * 16, sy_ + 4 * 16);

    if (parts_)
      for (int i = 0; i < NP; ++i) {
        Particle& q = parts_[i];
        if (!q.life) continue;
        --q.life;
        q.x = int16_t(q.x + q.vx);
        q.y = int16_t(q.y + q.vy);
        q.vy = int16_t(q.vy + 1);  // Schwerkraft
        g.pixel(q.x >> 4, q.y >> 4, q.c);
      }

    // blinzeln: alle ~3 s für 8 Ticks
    const uint16_t fr = (ctx.tick() % 180) < 8 ? 1 : 0;
    g.blit(kBlob.frame(fr), sx_ >> 4, sy_ >> 4, flip_);
    g.blit(kIcon, 1, 1);
    g.text(19, 1, "A/B FLIP", DIM);
    g.text(19, 7, "X FUNKEN", DIM);
  }

  void spawn(Context& ctx, int x, int y) {
    for (int n = 0; n < 2; ++n) {
      for (int i = 0; i < NP; ++i) {
        Particle& q = parts_[i];
        if (q.life) continue;
        q.x = int16_t(x);
        q.y = int16_t(y);
        q.vx = int16_t(int(ctx.random(33)) - 16);
        q.vy = int16_t(-int(ctx.random(24)) - 4);
        q.life = uint8_t(20 + ctx.random(30));
        static constexpr uint16_t cols[] = {color::YELLOW, color::ORANGE, color::RED,
                                            color::WHITE};
        q.c = cols[ctx.random(4)];
        break;
      }
    }
  }

  int page_ = 0, scroll_ = 0;
  bool large_ = false;
  int sx_ = 28 * 16, sy_ = 14 * 16;
  uint8_t flip_ = 0;
  Particle* parts_ = nullptr;
};

}  // namespace

PD_REGISTER_GAME(Testbild, {
  .id          = "testbild",
  .name        = "TESTBILD",
  .icon        = kIcon,
  .scoreMode   = ScoreMode::None,
  .minPlayers  = 1,
  .maxPlayers  = 1,
  .saveVersion = 1,
  .stickAsDpad = false,
})
