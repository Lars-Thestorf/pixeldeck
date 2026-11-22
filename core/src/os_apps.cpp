#include "os_apps.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "pd/context.h"
#include "pd/hal/net.h"
#include "pd/os/runtime.h"
#include "runtime_impl.h"
#include "score_table.h"

namespace pd::os {

namespace {
constexpr uint16_t DIM = rgb(60, 60, 60);
constexpr uint16_t TXT = rgb(200, 200, 200);
constexpr uint16_t SEL = rgb(255, 210, 0);
constexpr uint16_t BG = rgb(0, 0, 20);
constexpr uint16_t FRAME = rgb(50, 50, 110);

ScoreTable g_scores;  // Launcher und Systemmenü, nie gleichzeitig aktiv

const char* deviceTag(hal::input::DeviceType t) {
  switch (t) {
    case hal::input::DeviceType::Builtin: return "KON";
    case hal::input::DeviceType::Keyboard: return "TAS";
    case hal::input::DeviceType::Gamepad: return "PAD";
    case hal::input::DeviceType::WebPad: return "WEB";
  }
  return "?";
}

void center(Gfx& g, int y, const char* s, uint16_t c, Font f = Font::Small) {
  g.text((Gfx::W - g.textWidth(s, f)) / 2, y, s, c, f);
}

// Nächstes Profil für einen Slot: Gast und alle Profile, die kein anderer Slot nutzt.
uint8_t cycleProfile(Runtime& rt, int slot, int dir) {
  InputSystem& is = rt.inputSys();
  Profiles& pr = rt.profiles();
  uint8_t ids[1 + Profiles::MAX];
  int n = 0, cur = 0;
  ids[n++] = kGuest;
  for (int i = 0; i < pr.count(); ++i) {
    const uint8_t id = pr.at(i).id;
    if (is.profileInUse(id, slot)) continue;
    if (id == is.slot(slot).profileId) cur = n;
    ids[n++] = id;
  }
  return ids[(cur + dir + n) % n];
}

bool hasScores(const GameMeta& m) { return m.scoreMode != ScoreMode::None; }
}  // namespace

// --- Launcher (SPEC 12.1) -------------------------------------------------------

void Launcher::enter() {
  sel_ = 0;
  slide_ = 0;
  recFor_ = -1;
  const char* last = rt_.settings().lastGame;
  for (size_t i = 0; i < gameCount(); ++i)
    if (pd::detail::streq(game(i).meta.id, last)) sel_ = int(i);
}

void Launcher::loadRecord() {
  if (recFor_ == sel_) return;
  recFor_ = sel_;
  recValid_ = false;
  const GameMeta& m = game(size_t(sel_)).meta;
  if (!hasScores(m) || !g_scores.load(m.id) || g_scores.count == 0) return;
  recValid_ = true;
  recScore_ = g_scores.e[0].score;
  std::memcpy(recName_, g_scores.e[0].name, sizeof recName_);
}

void Launcher::tick(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  g.clear();

  if (in.pressed(Button::Start)) rt_.requestMenu(true);

  // Statuszeile: WebPad, Akku, belegte Slots als farbige Punkte
  if (rt_.webpad().running())
    g.text(39, 0, "W", rt_.webpad().clientCount() > 0 ? rgb(0, 220, 255) : rgb(40, 80, 200));
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
  const InputSystem& is = rt_.inputSys();
  for (int s = 0; s < InputSystem::SLOTS; ++s) {
    const InputSystem::Slot& sl = is.slot(s);
    uint16_t c = DIM;
    if (sl.used && (sl.present || (ctx.tick() / 15) % 2)) c = rt_.profiles().color(sl.profileId);
    g.fillRect(56 + 2 * s, 1, 1, 3, c);
  }

  const int n = int(gameCount());
  if (n == 0) {
    g.text(4, 12, "KEINE SPIELE", TXT);
    return;
  }

  if (in.pressed(Button::Left)) { sel_ = (sel_ + n - 1) % n; slide_ = -20; }
  if (in.pressed(Button::Right)) { sel_ = (sel_ + 1) % n; slide_ = 20; }
  if (in.pressed(Button::A)) rt_.requestStart(&game(size_t(sel_)));

  char buf[24];
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

  if (hasScores(m)) {
    loadRecord();
    if (!recValid_) {
      g.text(tx, 25, "REK ---", DIM);
    } else {
      // Rekord und Profilname (Namenskopie), Name nur, wenn er noch passt
      std::snprintf(buf, sizeof buf, "%d", int(recScore_));
      const int w = g.text(tx, 25, buf, SEL);
      if (w + 4 + g.textWidth(recName_) <= 38) g.text(tx + w + 4, 25, recName_, TXT);
    }
  }
}

// --- Lobby (SPEC 6.4) -------------------------------------------------------------

void Lobby::tick(Context& ctx) {
  Gfx& g = ctx.gfx;
  g.clear();
  const GameInfo* gi = rt_.lobbyGame();
  if (!gi) return;
  const GameMeta& m = gi->meta;
  if (ctx.input.pressed(Button::B)) {
    rt_.cancelLobby();
    return;
  }
  const InputSystem& is = rt_.inputSys();
  const int have = is.presentCount(m.maxPlayers);
  if (have >= m.minPlayers) rt_.requestStart(gi);

  if (g.textWidth(m.name, Font::Large) <= 62) center(g, 1, m.name, color::WHITE, Font::Large);
  else center(g, 2, m.name, color::WHITE);
  center(g, 10, "A: MITSPIELEN", (ctx.tick() / 30) % 2 ? SEL : TXT);
  char buf[12];
  std::snprintf(buf, sizeof buf, "%d/%u", have, unsigned(m.minPlayers));
  center(g, 17, buf, TXT, Font::Large);
  const int x0 = (Gfx::W - (m.maxPlayers * 6 - 2)) / 2;
  for (int s = 0; s < m.maxPlayers; ++s) {
    const InputSystem::Slot& sl = is.slot(s);
    const bool on = sl.used && sl.present;
    if (on) g.fillRect(x0 + 6 * s, 27, 4, 4, rt_.profiles().color(sl.profileId));
    else g.rect(x0 + 6 * s, 27, 4, 4, DIM);
  }
  g.text(0, 27, "B", DIM);
}

// --- Systemmenü (SPEC 12.2) -----------------------------------------------------

void SystemMenu::enter(bool inGame) {
  count_ = 0;
  if (inGame) {
    items_[count_++] = Item::Resume;
    items_[count_++] = Item::Quit;
  }
  items_[count_++] = Item::Players;
  items_[count_++] = Item::Profiles;
  items_[count_++] = Item::Scores;
  items_[count_++] = Item::WebPad;
  items_[count_++] = Item::Brightness;
  if (!inGame) items_[count_++] = Item::Back;
  sel_ = 0;
  page_ = Page::Main;
  plSwap_ = -1;
  edConfirm_ = scConfirm_ = false;
}

void SystemMenu::tick(Context& ctx) {
  if (ctx.input.pressed(Button::Start)) {  // Start schließt von jeder Seite
    rt_.requestMenu(false);
    return;
  }
  switch (page_) {
    case Page::Main: tickMain(ctx); break;
    case Page::Players: tickPlayers(ctx); break;
    case Page::Profiles: tickProfiles(ctx); break;
    case Page::Edit: tickEdit(ctx); break;
    case Page::Scores: tickScores(ctx); break;
    case Page::WebPad: tickWebPad(ctx); break;
  }
}

void SystemMenu::tickMain(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;

  if (in.pressed(Button::Up)) sel_ = (sel_ + count_ - 1) % count_;
  if (in.pressed(Button::Down)) sel_ = (sel_ + 1) % count_;
  const Item cur = items_[sel_];
  if (in.pressed(Button::B)) rt_.requestMenu(false);

  if (cur == Item::Brightness) {
    int lvl = rt_.settings().brightnessLevel;
    if (in.pressed(Button::Left)) rt_.setBrightnessLevel(lvl - 1);
    if (in.pressed(Button::Right)) rt_.setBrightnessLevel(lvl + 1);
    if (in.pressed(Button::A)) rt_.setBrightnessLevel((lvl + 1) % kBrightnessLevels);
  } else if (in.pressed(Button::A)) {
    switch (cur) {
      case Item::Resume:
      case Item::Back: rt_.requestMenu(false); break;
      case Item::Quit: rt_.requestEndGame(); break;
      case Item::Players: page_ = Page::Players; plSel_ = 0; plSwap_ = -1; break;
      case Item::Profiles: page_ = Page::Profiles; prSel_ = 0; break;
      case Item::WebPad: page_ = Page::WebPad; break;
      case Item::Scores:
        page_ = Page::Scores;
        scTop_ = 0;
        scConfirm_ = false;
        scLoaded_ = false;
        // aktuelles Spiel vorwählen, sonst das erste mit Highscores
        scGame_ = -1;
        for (size_t i = 0; i < gameCount(); ++i) {
          if (!hasScores(game(i).meta)) continue;
          if (scGame_ < 0) scGame_ = int(i);
          if (pd::detail::streq(game(i).meta.id, rt_.settings().lastGame)) scGame_ = int(i);
        }
        break;
      default: break;
    }
    if (page_ != Page::Main) return;  // nächste Seite zeichnet ab dem nächsten Tick
  }

  // Panel über dem abgedunkelten Bild; nur das Panel wird jeden Tick neu gezeichnet.
  // Drei Zeilen sichtbar, das Fenster folgt der Auswahl.
  g.fillRect(4, 0, 56, 32, BG);
  g.rect(4, 0, 56, 32, FRAME);
  const int top = sel_ < 2 ? 0 : (sel_ > count_ - 2 ? count_ - 3 : sel_ - 1);
  for (int row = 0; row < 3 && top + row < count_; ++row) {
    const int i = top + row;
    const int y = 2 + row * 10;
    const bool s = i == sel_;
    const uint16_t c = s ? SEL : TXT;
    if (s) g.text(7, y, ">", c, Font::Large);
    switch (items_[i]) {
      case Item::Resume: g.text(14, y, "WEITER", c, Font::Large); break;
      case Item::Quit: g.text(14, y, "BEENDEN", c, Font::Large); break;
      case Item::Back: g.text(14, y, "ZURUECK", c, Font::Large); break;
      case Item::Players: g.text(14, y, "SPIELER", c, Font::Large); break;
      case Item::Profiles: g.text(14, y, "PROFILE", c, Font::Large); break;
      case Item::Scores: g.text(14, y, "REKORDE", c, Font::Large); break;
      case Item::WebPad:
        g.text(14, y, "WEBPAD", c, Font::Large);
        if (rt_.webpad().running()) g.fillRect(52, y + 2, 3, 3, rgb(60, 200, 60));
        break;
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
  // Rollbalken
  if (count_ > 3) {
    const int h = 30 * 3 / count_;
    g.vline(58, 1 + (30 - h) * top / (count_ - 3), h, DIM);
  }
}

// Spieler: Hoch/Runter Slot, Links/Rechts Profil, A tauschen (zweimal), X entfernen, B zurück.
void SystemMenu::tickPlayers(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  InputSystem& is = rt_.inputSys();
  const int S = InputSystem::SLOTS;

  if (in.pressed(Button::Up)) plSel_ = (plSel_ + S - 1) % S;
  if (in.pressed(Button::Down)) plSel_ = (plSel_ + 1) % S;
  const bool used = is.slot(plSel_).used;
  if (used && in.pressed(Button::Left)) rt_.setSlotProfile(plSel_, cycleProfile(rt_, plSel_, -1));
  if (used && in.pressed(Button::Right)) rt_.setSlotProfile(plSel_, cycleProfile(rt_, plSel_, 1));
  if (in.pressed(Button::A)) {
    if (plSwap_ < 0) plSwap_ = plSel_;
    else {
      if (plSwap_ != plSel_) is.swapSlots(plSwap_, plSel_);
      plSwap_ = -1;
    }
  }
  if (in.pressed(Button::X)) is.removeSlot(plSel_);
  if (in.pressed(Button::B)) {
    if (plSwap_ >= 0) plSwap_ = -1;
    else { page_ = Page::Main; return; }
  }

  g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
  const int prim = is.primarySlot();
  for (int s = 0; s < S; ++s) {
    const InputSystem::Slot& sl = is.slot(s);
    const int y = 1 + s * 8;
    const bool sel = s == plSel_;
    const bool blink = (ctx.tick() / 10) % 2;
    if (sel) g.text(0, y, ">", SEL);
    char num[2] = {char('1' + s), 0};
    g.text(4, y, num, s == plSwap_ && blink ? color::BLACK : (s == plSwap_ ? SEL : TXT));
    if (!sl.used) {
      g.text(12, y, "-", DIM);
      continue;
    }
    const uint16_t pc = rt_.profiles().color(sl.profileId);
    g.fillRect(8, y, 2, 5, pc);
    g.text(12, y, rt_.profiles().name(sl.profileId), sel ? SEL : TXT);
    const char* tag = deviceTag(sl.type);
    g.text(52, y, tag, !sl.present ? (blink ? DIM : BG) : (s == prim ? TXT : DIM));
  }
}

// Profile: Liste mit "+ NEU" am Ende; A öffnet den Editor.
void SystemMenu::tickProfiles(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  Profiles& pr = rt_.profiles();
  const int rows = pr.count() + (pr.count() < Profiles::MAX ? 1 : 0);
  if (prSel_ >= rows) prSel_ = rows - 1;

  if (in.pressed(Button::Up)) prSel_ = (prSel_ + rows - 1) % rows;
  if (in.pressed(Button::Down)) prSel_ = (prSel_ + 1) % rows;
  if (in.pressed(Button::B)) { page_ = Page::Main; return; }
  if (in.pressed(Button::A)) { openEdit(prSel_ < pr.count() ? prSel_ : -1); return; }

  g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
  const int top = prSel_ < 4 ? 0 : prSel_ - 3;
  for (int row = 0; row < 4 && top + row < rows; ++row) {
    const int i = top + row;
    const int y = 1 + row * 8;
    const bool sel = i == prSel_;
    if (sel) g.text(0, y, ">", SEL);
    if (i < pr.count()) {
      const Profile& p = pr.at(i);
      g.fillRect(5, y, 2, 5, kProfileColor[p.color % kProfileColors]);
      g.text(9, y, p.name, sel ? SEL : TXT);
    } else {
      g.text(5, y, "+ NEU", sel ? SEL : TXT);
    }
  }
  char buf[8];
  std::snprintf(buf, sizeof buf, "%d/%d", pr.count(), Profiles::MAX);
  g.text(Gfx::W - g.textWidth(buf), 1, buf, DIM);
}

namespace {
constexpr char kCharset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
constexpr int kCharsetLen = int(sizeof kCharset) - 1;
int charIndex(char c) {
  for (int i = 0; i < kCharsetLen; ++i)
    if (kCharset[i] == c) return i;
  return 0;
}
}  // namespace

void SystemMenu::openEdit(int profileIndex) {
  Profiles& pr = rt_.profiles();
  page_ = Page::Edit;
  edIndex_ = profileIndex;
  edCursor_ = 0;
  edConfirm_ = false;
  std::memset(edName_, ' ', kNameLen);
  edName_[kNameLen] = 0;
  if (profileIndex >= 0) {
    const Profile& p = pr.at(profileIndex);
    std::memcpy(edName_, p.name, std::strlen(p.name));
    edColor_ = p.color;
  } else {
    edName_[0] = 'A';
    edColor_ = uint8_t(pr.count() % kProfileColors);
  }
}

// Editor: Hoch/Runter Zeichen, Links/Rechts Cursor, X Farbe, A speichern, B abbrechen,
// Y löschen (mit Bestätigung).
void SystemMenu::tickEdit(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  Profiles& pr = rt_.profiles();
  if (edIndex_ >= pr.count()) { page_ = Page::Profiles; return; }

  if (edConfirm_) {
    if (in.pressed(Button::A)) {
      rt_.deleteProfile(pr.at(edIndex_).id);
      page_ = Page::Profiles;
      return;
    }
    if (in.pressed(Button::B)) edConfirm_ = false;
    g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
    center(g, 2, pr.at(edIndex_).name, kProfileColor[pr.at(edIndex_).color % kProfileColors], Font::Large);
    center(g, 13, "LOESCHEN?", rgb(255, 60, 40), Font::Large);
    center(g, 25, "A JA  B NEIN", TXT);
    return;
  }

  if (in.pressed(Button::Left)) edCursor_ = (edCursor_ + kNameLen - 1) % kNameLen;
  if (in.pressed(Button::Right)) edCursor_ = (edCursor_ + 1) % kNameLen;
  if (in.pressed(Button::Up))
    edName_[edCursor_] = kCharset[(charIndex(edName_[edCursor_]) + 1) % kCharsetLen];
  if (in.pressed(Button::Down))
    edName_[edCursor_] = kCharset[(charIndex(edName_[edCursor_]) + kCharsetLen - 1) % kCharsetLen];
  if (in.pressed(Button::X)) edColor_ = uint8_t((edColor_ + 1) % kProfileColors);
  if (in.pressed(Button::B)) { page_ = Page::Profiles; return; }
  if (in.pressed(Button::Y) && edIndex_ >= 0) edConfirm_ = true;
  if (in.pressed(Button::A)) {
    bool ok;
    if (edIndex_ >= 0) {
      ok = pr.update(pr.at(edIndex_).id, edName_, edColor_);
    } else {
      const uint8_t id = pr.create(edName_, edColor_);
      ok = id != 0;
      if (ok) prSel_ = pr.indexOf(id);
    }
    if (ok) {
      rt_.saveProfiles();
      page_ = Page::Profiles;
      return;
    }
    // leerer Name: bleibt im Editor
  }

  g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
  g.text(0, 0, edIndex_ < 0 ? "NEUES PROFIL" : "PROFIL", DIM);
  const uint16_t c = kProfileColor[edColor_];
  const int x0 = (Gfx::W - (kNameLen * 6 - 1)) / 2;
  for (int i = 0; i < kNameLen; ++i) {
    const char ch[2] = {edName_[i], 0};
    g.text(x0 + i * 6, 9, ch, c, Font::Large);
    if (i == edCursor_ && (ctx.tick() / 12) % 2 == 0) g.hline(x0 + i * 6, 17, 5, color::WHITE);
    else if (edName_[i] == ' ') g.hline(x0 + i * 6 + 1, 17, 3, DIM);
  }
  g.text(0, 21, "A OK B ABBRUCH", DIM);
  g.text(0, 27, edIndex_ >= 0 ? "X FARBE Y LOESCH" : "X FARBE", DIM);
}

void SystemMenu::loadScores() {
  if (scGame_ < 0) return;
  g_scores.load(game(size_t(scGame_)).meta.id);
  scLoaded_ = true;
}

// Highscores: Links/Rechts Spiel, Hoch/Runter blättern, X zurücksetzen (mit Bestätigung).
void SystemMenu::tickScores(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
  if (scGame_ < 0) {
    if (in.pressed(Button::B) || in.pressed(Button::A)) page_ = Page::Main;
    center(g, 13, "KEINE REKORDE", TXT);
    return;
  }

  const int n = int(gameCount());
  if (!scConfirm_) {
    for (int dir : {-1, 1}) {
      if (!in.pressed(dir < 0 ? Button::Left : Button::Right)) continue;
      int i = scGame_;
      for (int k = 0; k < n; ++k) {
        i = (i + dir + n) % n;
        if (hasScores(game(size_t(i)).meta)) break;
      }
      if (i != scGame_) { scGame_ = i; scTop_ = 0; scLoaded_ = false; }
    }
    if (!scLoaded_) loadScores();
    const int maxTop = g_scores.count > 4 ? g_scores.count - 4 : 0;
    if (in.pressed(Button::Up) && scTop_ > 0) --scTop_;
    if (in.pressed(Button::Down) && scTop_ < maxTop) ++scTop_;
    if (in.pressed(Button::X) && g_scores.count > 0) scConfirm_ = true;
    if (in.pressed(Button::B)) { page_ = Page::Main; return; }
  } else {
    if (in.pressed(Button::A)) {
      ScoreTable::reset(game(size_t(scGame_)).meta.id);
      scConfirm_ = false;
      scTop_ = 0;
      loadScores();
    } else if (in.pressed(Button::B)) {
      scConfirm_ = false;
    }
  }

  const GameMeta& m = game(size_t(scGame_)).meta;
  center(g, 0, m.name, color::WHITE);
  g.text(0, 0, "<", DIM);
  g.text(61, 0, ">", DIM);
  if (scConfirm_) {
    center(g, 10, "ZURUECKSETZEN?", rgb(255, 60, 40));
    center(g, 20, "A JA  B NEIN", TXT);
    return;
  }
  if (g_scores.count == 0) {
    center(g, 14, "NOCH LEER", DIM);
    return;
  }
  char buf[16];
  for (int row = 0; row < 4 && scTop_ + row < g_scores.count; ++row) {
    const int i = scTop_ + row;
    const ScoreEntry& e = g_scores.e[i];
    const int y = 7 + row * 6;
    std::snprintf(buf, sizeof buf, "%d", i + 1);
    g.text(7 - g.textWidth(buf), y, buf, i == 0 ? SEL : DIM);
    g.text(10, y, e.name, rt_.profiles().find(e.profileId) ? rt_.profiles().color(e.profileId) : TXT);
    std::snprintf(buf, sizeof buf, "%d", int(e.score));
    g.text(Gfx::W - g.textWidth(buf), y, buf, TXT);
  }
}

// WebPad (SPEC 13): A schaltet ein/aus, X erzeugt ein neues WLAN-Passwort, B zurück.
void SystemMenu::tickWebPad(Context& ctx) {
  Gfx& g = ctx.gfx;
  Input& in = ctx.input;
  WebPad& wp = rt_.webpad();
  if (in.pressed(Button::B)) { page_ = Page::Main; return; }
  if (in.pressed(Button::A)) rt_.setWebPad(!wp.running());
  const hal::net::Info info = wp.running() ? hal::net::info() : hal::net::Info{};
  if (in.pressed(Button::X) && wp.running() && info.accessPoint) rt_.newWebPadPassword();

  g.fillRect(0, 0, Gfx::W, Gfx::H, BG);
  g.text(0, 0, "WEBPAD", TXT);
  char buf[24];
  if (!wp.running()) {
    const bool failed = rt_.settings().webpadOn;  // eingeschaltet, aber Netz nicht gestartet
    g.text(Gfx::W - g.textWidth("AUS"), 0, "AUS", failed ? rgb(255, 60, 40) : DIM);
    center(g, 9, failed ? "NETZ FEHLER" : "HANDY ALS PAD", failed ? rgb(255, 60, 40) : DIM);
    center(g, 19, "A EINSCHALTEN", (ctx.tick() / 30) % 2 ? SEL : TXT);
    return;
  }
  std::snprintf(buf, sizeof buf, "AN %d", wp.clientCount());
  g.text(Gfx::W - g.textWidth(buf), 0, buf, rgb(60, 200, 60));
  const Settings& st = rt_.settings();
  if (info.accessPoint) {
    std::snprintf(buf, sizeof buf, "PIXELDECK-%s", st.apSuffix);
    g.text(0, 7, buf, color::WHITE);
    std::snprintf(buf, sizeof buf, "PW %s", st.apPass);
    g.text(0, 13, buf, color::WHITE);
    g.text(0, 19, info.host, TXT);
    g.text(0, 26, "A AUS X NEUES PW", DIM);
  } else {
    g.text(0, 7, "IM NETZ DES PC", DIM);
    g.text(0, 13, info.host, color::WHITE);
    std::snprintf(buf, sizeof buf, "PORT %u", unsigned(info.port));
    g.text(0, 19, buf, color::WHITE);
    g.text(0, 26, "A AUS", DIM);
  }
}

}  // namespace pd::os
