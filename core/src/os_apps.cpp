#include "os_apps.h"

#include <cstdio>

#include "pd/context.h"
#include "pd/os/runtime.h"
#include "runtime_impl.h"

namespace pd::os {

namespace {
constexpr uint16_t DIM = rgb(60, 60, 60);
constexpr uint16_t TXT = rgb(200, 200, 200);
constexpr uint16_t SEL = rgb(255, 210, 0);
}  // namespace

// --- Launcher (SPEC 12.1) -------------------------------------------------------

void Launcher::enter() {
  sel_ = 0;
  slide_ = 0;
  const char* last = rt_.settings().lastGame;
  for (size_t i = 0; i < gameCount(); ++i)
    if (pd::detail::streq(game(i).meta.id, last)) sel_ = int(i);
}

void Launcher::tick(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  g.clear();

  if (in.pressed(Button::Start)) rt_.requestMenu(true);

  // Statuszeile: Akku, belegte Slots als farbige Punkte (WebPad folgt mit M6)
  const hal::power::PowerState& pw = rt_.power();
  if (pw.available) {
    // Batteriesymbol 7x3 + Pol, Füllung 0-5 px
    const int fill = (pw.percent * 5 + 50) / 100;
    const uint16_t c = pw.charging ? rgb(255, 210, 0)
                       : pw.percent < 10 ? rgb(255, 40, 30) : rgb(60, 200, 60);
    g.rect(45, 0, 7, 5, DIM);
    g.fillRect(52, 1, 1, 3, DIM);
    if (fill > 0) g.fillRect(46, 1, fill, 3, c);
  }
  for (int s = 0; s < Input::MAX_PLAYERS; ++s) {
    Player& p = in.player(s);
    g.fillRect(56 + 2 * s, 1, 1, 3, p.connected() ? p.color() : DIM);
  }

  const int n = int(gameCount());
  if (n == 0) {
    g.text(4, 12, "KEINE SPIELE", TXT);
    return;
  }

  if (in.pressed(Button::Left)) { sel_ = (sel_ + n - 1) % n; slide_ = -20; }
  if (in.pressed(Button::Right)) { sel_ = (sel_ + 1) % n; slide_ = 20; }
  if (in.pressed(Button::A)) rt_.requestStart(&game(size_t(sel_)));

  char buf[16];
  std::snprintf(buf, sizeof buf, "%d/%d", sel_ + 1, n);
  g.text(0, 0, buf, DIM);

  const GameMeta& m = game(size_t(sel_)).meta;
  const int ox = slide_;
  slide_ = slide_ * 2 / 3;

  if (n > 1) {
    g.text(0, 15, "<", in.held(Button::Left) ? SEL : DIM);
    g.text(61, 15, ">", in.held(Button::Right) ? SEL : DIM);
  }
  g.blit(m.icon, 5 + ox, 9);

  const int tx = 24 + ox;
  if (g.textWidth(m.name, Font::Large) <= 38) g.text(tx, 9, m.name, color::WHITE, Font::Large);
  else g.text(tx, 10, m.name, color::WHITE);

  if (m.minPlayers == m.maxPlayers) std::snprintf(buf, sizeof buf, "%uP", unsigned(m.maxPlayers));
  else std::snprintf(buf, sizeof buf, "%u-%uP", unsigned(m.minPlayers), unsigned(m.maxPlayers));
  g.text(tx, 19, buf, TXT);
  if (m.scoreMode != ScoreMode::None) g.text(tx, 25, "REK ---", DIM);  // Highscores: M4
}

// --- Systemmenü (SPEC 12.2) -----------------------------------------------------

void SystemMenu::enter(bool inGame) {
  count_ = 0;
  if (inGame) {
    items_[count_++] = Item::Resume;
    items_[count_++] = Item::Quit;
  }
  items_[count_++] = Item::Brightness;
  if (!inGame) items_[count_++] = Item::Back;
  sel_ = 0;
}

void SystemMenu::tick(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;

  if (in.pressed(Button::Up)) sel_ = (sel_ + count_ - 1) % count_;
  if (in.pressed(Button::Down)) sel_ = (sel_ + 1) % count_;
  const Item cur = items_[sel_];
  if (in.pressed(Button::B) || in.pressed(Button::Start)) rt_.requestMenu(false);

  if (cur == Item::Brightness) {
    int lvl = rt_.settings().brightnessLevel;
    if (in.pressed(Button::Left)) rt_.setBrightnessLevel(lvl - 1);
    if (in.pressed(Button::Right)) rt_.setBrightnessLevel(lvl + 1);
    if (in.pressed(Button::A)) rt_.setBrightnessLevel((lvl + 1) % kBrightnessLevels);
  } else if (in.pressed(Button::A)) {
    if (cur == Item::Resume || cur == Item::Back) rt_.requestMenu(false);
    if (cur == Item::Quit) rt_.requestEndGame();
  }

  // Panel über dem abgedunkelten Bild; nur das Panel wird jeden Tick neu gezeichnet.
  g.fillRect(4, 0, 56, 32, rgb(0, 0, 20));
  g.rect(4, 0, 56, 32, rgb(50, 50, 110));
  for (int i = 0; i < count_; ++i) {
    const int y = 2 + i * 10;
    const bool s = i == sel_;
    const uint16_t c = s ? SEL : TXT;
    if (s) g.text(7, y, ">", c, Font::Large);
    switch (items_[i]) {
      case Item::Resume: g.text(14, y, "WEITER", c, Font::Large); break;
      case Item::Quit: g.text(14, y, "BEENDEN", c, Font::Large); break;
      case Item::Back: g.text(14, y, "ZURUECK", c, Font::Large); break;
      case Item::Brightness: {
        g.text(14, y, "HELL", c, Font::Large);
        const int lvl = rt_.settings().brightnessLevel;
        for (int b = 0; b < kBrightnessLevels; ++b) {
          const int h = 1 + b * 6 / (kBrightnessLevels - 1);
          g.vline(40 + 2 * b, y + 7 - h, h, b <= lvl ? c : DIM);
        }
        break;
      }
    }
  }
}

}  // namespace pd::os
