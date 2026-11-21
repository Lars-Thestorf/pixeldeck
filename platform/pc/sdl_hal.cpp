// HAL-Implementierung für den PC-Simulator (SDL3).
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "pd/hal/app.h"
#include "pd/hal/display.h"
#include "pd/hal/input.h"
#include "pd/hal/log.h"
#include "pd/hal/power.h"
#include "pd/hal/random.h"
#include "pd/hal/time.h"
#include "pd/input.h"
#include "sim.h"

namespace pd::pc {
namespace {

constexpr int MW = 64, MH = 32;  // Matrix

struct State {
  SimConfig cfg;
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;    // 64x32 RGB565
  SDL_Texture* mask = nullptr;   // LED-Maske in Zielgröße
  int maskScale = 0;
  uint8_t brightness = 255;
  bool quit = false;
  bool videoOk = false;
  uint16_t lastFrame[MW * MH] = {};
  uint64_t virtualUs = 0;        // Uhr im Headless-Modus
  uint32_t fuzzState = 1;
  uint16_t fuzzButtons = 0;
  int8_t fuzzAxes[4] = {};
  struct Pad {
    SDL_Gamepad*   gp = nullptr;
    SDL_JoystickID jid = 0;
    uint32_t       id = 0;
    char           name[16] = {};
  };
  static constexpr int kMaxPads = 6;
  Pad pads[kMaxPads];
} g;

// Stabile Geräte-ID eines Gamepads: GUID + Seriennummer, bei gleichen Geräten ohne
// Seriennummer zusätzlich die Reihenfolge. Hohes Bit gesetzt (Tastaturen: 1, 2).
uint32_t padId(SDL_Gamepad* gp) {
  char guid[64] = {};
  SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(gp)), guid, sizeof guid);
  uint32_t h = 2166136261u;
  auto mix = [&h](const char* s) {
    for (; s && *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
  };
  mix(guid);
  mix(SDL_GetGamepadSerial(gp));
  for (;;) {  // gleiche ID schon vergeben -> weiterzählen
    bool dup = false;
    for (const State::Pad& p : g.pads)
      if (p.gp && p.id == (h | 0x80000000u)) dup = true;
    if (!dup) break;
    h = h * 16777619u + 1;
  }
  return h | 0x80000000u;
}

void openPad(SDL_JoystickID jid) {
  for (State::Pad& p : g.pads)
    if (p.gp && p.jid == jid) return;
  for (State::Pad& p : g.pads) {
    if (p.gp) continue;
    SDL_Gamepad* gp = SDL_OpenGamepad(jid);
    if (!gp) { std::fprintf(stderr, "[WARN] Gamepad: %s\n", SDL_GetError()); return; }
    p.id = padId(gp);
    p.gp = gp;
    p.jid = jid;
    const char* n = SDL_GetGamepadName(gp);
    int k = 0;
    for (; n && n[k] && k < int(sizeof p.name) - 1; ++k) {
      char c = n[k];
      if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
      p.name[k] = (c >= 32 && c <= 126) ? c : ' ';
    }
    if (k == 0) std::snprintf(p.name, sizeof p.name, "GAMEPAD");
    std::fprintf(stderr, "[INFO] Gamepad verbunden: %s (id %08x), A drücken zum Mitspielen\n",
                 n ? n : "?", unsigned(p.id));
    return;
  }
  std::fprintf(stderr, "[WARN] Gamepad ignoriert, schon %d verbunden\n", State::kMaxPads);
}

void closePad(SDL_JoystickID jid) {
  for (State::Pad& p : g.pads)
    if (p.gp && p.jid == jid) {
      std::fprintf(stderr, "[INFO] Gamepad getrennt: %s\n", p.name);
      SDL_CloseGamepad(p.gp);
      p = State::Pad{};
    }
}

// -32768..32767 -> -127..127 mit Totzone [Annahme] ca. 25 %
int8_t padAxis(SDL_Gamepad* gp, SDL_GamepadAxis a) {
  constexpr int kDead = 8000;
  int v = SDL_GetGamepadAxis(gp, a);
  const int mag = v < 0 ? -v : v;
  if (mag <= kDead) return 0;
  int out = (mag - kDead) * 127 / (32767 - kDead);
  if (out > 127) out = 127;
  return int8_t(v < 0 ? -out : out);
}

uint32_t fuzzNext() {  // xorshift32
  uint32_t x = g.fuzzState;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return g.fuzzState = x;
}

void logSdlError(const char* what) {
  std::fprintf(stderr, "[ERROR] %s: %s\n", what, SDL_GetError());
}

// Maske: dunkle Fläche mit runden, transparenten Löchern pro LED (kantengeglättet).
void rebuildMask(int s) {
  if (g.mask) { SDL_DestroyTexture(g.mask); g.mask = nullptr; }
  g.maskScale = s;
  const int w = MW * s, h = MH * s;
  std::vector<uint8_t> px(size_t(w) * h * 4);
  const float r = s * 0.42f, c = (s - 1) * 0.5f;
  std::vector<uint8_t> cell(size_t(s) * s);
  for (int y = 0; y < s; ++y)
    for (int x = 0; x < s; ++x) {
      float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
      float a = d - r + 0.5f;
      a = a < 0 ? 0 : (a > 1 ? 1 : a);
      cell[size_t(y) * s + x] = uint8_t(a * 255.0f + 0.5f);
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* p = &px[(size_t(y) * w + x) * 4];
      p[0] = p[1] = p[2] = 10;
      p[3] = cell[size_t(y % s) * s + (x % s)];
    }
  g.mask = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!g.mask) { logSdlError("Maske"); return; }
  SDL_UpdateTexture(g.mask, nullptr, px.data(), w * 4);
  SDL_SetTextureBlendMode(g.mask, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(g.mask, SDL_SCALEMODE_NEAREST);
}

void handleEvents() {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_EVENT_QUIT:
        g.quit = true;
        break;
      case SDL_EVENT_GAMEPAD_ADDED:
        openPad(ev.gdevice.which);
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        closePad(ev.gdevice.which);
        break;
      case SDL_EVENT_KEY_DOWN:
        if (ev.key.repeat) break;
        if (ev.key.scancode == SDL_SCANCODE_ESCAPE) g.quit = true;
        if (ev.key.scancode == SDL_SCANCODE_F2) {
          g.cfg.led = !g.cfg.led;
          std::fprintf(stderr, "[INFO] Anzeige: %s\n", g.cfg.led ? "LED-Optik" : "Pixel");
        }
        break;
      default:
        break;
    }
  }
}

int8_t axis(const bool* k, SDL_Scancode neg, SDL_Scancode pos) {
  return int8_t((k[pos] ? 127 : 0) - (k[neg] ? 127 : 0));
}

}  // namespace

SimConfig& config() { return g.cfg; }

bool writeLastFrame(const char* path) { return writePng(path, g.lastFrame, MW, MH); }

void shutdown() {
  for (State::Pad& p : g.pads)
    if (p.gp) SDL_CloseGamepad(p.gp);
  if (g.mask) SDL_DestroyTexture(g.mask);
  if (g.tex) SDL_DestroyTexture(g.tex);
  if (g.ren) SDL_DestroyRenderer(g.ren);
  if (g.win) SDL_DestroyWindow(g.win);
  g = State{};
  SDL_Quit();
}

}  // namespace pd::pc

// ---------------------------------------------------------------------------
namespace pd::hal {
using pd::pc::g;

namespace display {

void init() {
  if (g.videoOk) return;
  g.fuzzState = g.cfg.fuzzSeed ? g.cfg.fuzzSeed : 1;
  if (g.cfg.headless) return;
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { pd::pc::logSdlError("SDL_Init"); g.quit = true; return; }
  const int s = g.cfg.scale < 1 ? 1 : g.cfg.scale;
  g.win = SDL_CreateWindow("pixeldeck", pd::pc::MW * s, pd::pc::MH * s,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!g.win) { pd::pc::logSdlError("SDL_CreateWindow"); g.quit = true; return; }
  g.ren = SDL_CreateRenderer(g.win, nullptr);
  if (!g.ren) { pd::pc::logSdlError("SDL_CreateRenderer"); g.quit = true; return; }
  g.tex = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                            pd::pc::MW, pd::pc::MH);
  if (!g.tex) { pd::pc::logSdlError("SDL_CreateTexture"); g.quit = true; return; }
  SDL_SetTextureScaleMode(g.tex, SDL_SCALEMODE_NEAREST);
  g.videoOk = true;
  std::fprintf(stderr,
               "[INFO] Simulator: Esc beendet, F2 schaltet LED-Optik/Pixel. Tastatur 1 rechts "
               "(Pfeile, ; . O K), Tastatur 2 links (WASD, H B T F), Gamepads per A. "
               "Tasten nach US-Position.\n");
}

void present(const uint16_t* fb) {
  std::memcpy(g.lastFrame, fb, sizeof g.lastFrame);
  if (!g.videoOk) return;
  SDL_UpdateTexture(g.tex, nullptr, fb, pd::pc::MW * 2);

  int ow = 0, oh = 0;
  SDL_GetCurrentRenderOutputSize(g.ren, &ow, &oh);
  int s = ow / pd::pc::MW < oh / pd::pc::MH ? ow / pd::pc::MW : oh / pd::pc::MH;
  if (s < 1) s = 1;
  const SDL_FRect dst{float((ow - pd::pc::MW * s) / 2), float((oh - pd::pc::MH * s) / 2),
                      float(pd::pc::MW * s), float(pd::pc::MH * s)};
  const bool led = g.cfg.led && s >= 4;

  SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 255);
  SDL_RenderClear(g.ren);
  SDL_SetTextureColorMod(g.tex, g.brightness, g.brightness, g.brightness);
  if (led) {
    if (s != g.maskScale || !g.mask) pd::pc::rebuildMask(s);
    // unbeleuchtete LEDs schwach grau, Farbe additiv darüber
    SDL_SetRenderDrawColor(g.ren, 24, 24, 24, 255);
    SDL_RenderFillRect(g.ren, &dst);
    SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_ADD);
    SDL_RenderTexture(g.ren, g.tex, nullptr, &dst);
    if (g.mask) SDL_RenderTexture(g.ren, g.mask, nullptr, &dst);
  } else {
    SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(g.ren, g.tex, nullptr, &dst);
  }
  SDL_RenderPresent(g.ren);
}

void setBrightness(uint8_t level) { g.brightness = level; }

}  // namespace display

namespace input {

size_t poll(Device* out, size_t max) {
  if (max == 0) return 0;
  Device& d = out[0];
  d.id = 1;
  d.type = DeviceType::Keyboard;
  std::snprintf(d.name, sizeof d.name, "TASTATUR 1");
  d.connected = true;
  d.state = RawInput{0, 0, 0, 0, 0};

  if (g.cfg.fuzzInput) {
    // zufällig Buttons umschalten, Sticks gelegentlich neu setzen
    if ((pd::pc::fuzzNext() & 7) == 0) g.fuzzButtons ^= uint16_t(1u << (pd::pc::fuzzNext() % 14));
    if ((pd::pc::fuzzNext() & 31) == 0)
      g.fuzzAxes[pd::pc::fuzzNext() % 4] = int8_t(int(pd::pc::fuzzNext() % 255) - 127);
    d.state = RawInput{g.fuzzButtons, g.fuzzAxes[0], g.fuzzAxes[1], g.fuzzAxes[2], g.fuzzAxes[3]};
  }
  if (!g.videoOk) return 1;

  pd::pc::handleEvents();
  if (g.cfg.fuzzInput) return 1;

  // Tastatur, zwei Layouts (SPEC 15). Scancodes = physische US-Position.
  const bool* k = SDL_GetKeyboardState(nullptr);
  using B = pd::Button;
  struct Map { SDL_Scancode sc; B b; };
  // Layout 1, rechte Hälfte: Pfeile, Aktionstasten als Raute um L
  static const Map kMap1[] = {
    {SDL_SCANCODE_UP, B::Up},          {SDL_SCANCODE_DOWN, B::Down},
    {SDL_SCANCODE_LEFT, B::Left},      {SDL_SCANCODE_RIGHT, B::Right},
    {SDL_SCANCODE_SEMICOLON, B::A},    {SDL_SCANCODE_PERIOD, B::B},
    {SDL_SCANCODE_O, B::X},            {SDL_SCANCODE_K, B::Y},
    {SDL_SCANCODE_I, B::L},            {SDL_SCANCODE_P, B::R},
    {SDL_SCANCODE_RETURN, B::Start},   {SDL_SCANCODE_RSHIFT, B::Select},
    {SDL_SCANCODE_KP_0, B::L3},        {SDL_SCANCODE_PAGEUP, B::R3},
  };
  // Layout 2, linke Hälfte: WASD, Aktionstasten als Raute um G
  static const Map kMap2[] = {
    {SDL_SCANCODE_W, B::Up},           {SDL_SCANCODE_S, B::Down},
    {SDL_SCANCODE_A, B::Left},         {SDL_SCANCODE_D, B::Right},
    {SDL_SCANCODE_H, B::A},            {SDL_SCANCODE_B, B::B},
    {SDL_SCANCODE_T, B::X},            {SDL_SCANCODE_F, B::Y},
    {SDL_SCANCODE_Q, B::L},            {SDL_SCANCODE_E, B::R},
    {SDL_SCANCODE_2, B::Start},        {SDL_SCANCODE_1, B::Select},
  };
  uint16_t buttons = 0;
  for (const Map& m : kMap1)
    if (k[m.sc]) buttons |= uint16_t(m.b);
  d.state.buttons = buttons;
  d.state.lx = pd::pc::axis(k, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6);
  d.state.ly = pd::pc::axis(k, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5);
  d.state.rx = pd::pc::axis(k, SDL_SCANCODE_DELETE, SDL_SCANCODE_PAGEDOWN);
  d.state.ry = pd::pc::axis(k, SDL_SCANCODE_HOME, SDL_SCANCODE_END);

  size_t n = 1;
  if (n < max) {
    Device& d2 = out[n++];
    d2.id = 2;
    d2.type = DeviceType::Keyboard;
    std::snprintf(d2.name, sizeof d2.name, "TASTATUR 2");
    d2.connected = true;
    d2.state = RawInput{0, 0, 0, 0, 0};
    for (const Map& m : kMap2)
      if (k[m.sc]) d2.state.buttons |= uint16_t(m.b);
  }

  // SDL-Gamepads. Belegung nach Position wie am Nintendo-Pad [Annahme]:
  // rechts = A, unten = B, oben = X, links = Y. Trigger zählen zusätzlich als L/R.
  struct PadMap { SDL_GamepadButton sb; B b; };
  static const PadMap kPad[] = {
    {SDL_GAMEPAD_BUTTON_EAST, B::A},          {SDL_GAMEPAD_BUTTON_SOUTH, B::B},
    {SDL_GAMEPAD_BUTTON_NORTH, B::X},         {SDL_GAMEPAD_BUTTON_WEST, B::Y},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, B::L}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, B::R},
    {SDL_GAMEPAD_BUTTON_START, B::Start},     {SDL_GAMEPAD_BUTTON_BACK, B::Select},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, B::Up},      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, B::Down},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, B::Left},  {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, B::Right},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, B::L3},   {SDL_GAMEPAD_BUTTON_RIGHT_STICK, B::R3},
  };
  for (const auto& p : g.pads) {
    if (!p.gp || n >= max) continue;
    Device& pd_ = out[n++];
    pd_.id = p.id;
    pd_.type = DeviceType::Gamepad;
    std::memcpy(pd_.name, p.name, sizeof pd_.name);
    pd_.connected = SDL_GamepadConnected(p.gp);
    uint16_t b = 0;
    for (const PadMap& m : kPad)
      if (SDL_GetGamepadButton(p.gp, m.sb)) b |= uint16_t(m.b);
    if (SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) b |= uint16_t(B::L);
    if (SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) b |= uint16_t(B::R);
    pd_.state.buttons = b;
    pd_.state.lx = pd::pc::padAxis(p.gp, SDL_GAMEPAD_AXIS_LEFTX);
    pd_.state.ly = pd::pc::padAxis(p.gp, SDL_GAMEPAD_AXIS_LEFTY);
    pd_.state.rx = pd::pc::padAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHTX);
    pd_.state.ry = pd::pc::padAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHTY);
  }
  return n;
}

void rumble(uint32_t id, uint16_t ms) {  // Tastaturen: No-Op
  for (const auto& p : g.pads)
    if (p.gp && p.id == id) SDL_RumbleGamepad(p.gp, 0xC000, 0xC000, ms);
}

}  // namespace input

namespace time {

uint64_t micros() { return g.cfg.headless ? g.virtualUs : SDL_GetTicksNS() / 1000u; }

void sleepUntil(uint64_t target) {
  if (g.cfg.headless) {
    if (target > g.virtualUs) g.virtualUs = target;
    return;
  }
  // grob schlafen, die letzte Millisekunde aktiv warten (Genauigkeit des Takts)
  for (;;) {
    const uint64_t now = micros();
    if (now >= target) return;
    const uint64_t left = target - now;
    if (left > 1500) SDL_DelayNS((left - 1000) * 1000u);
  }
}

}  // namespace time

namespace log {

void write(Level lvl, const char* msg) {
  static const char* const kNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  const unsigned i = unsigned(lvl) < 4 ? unsigned(lvl) : 3;
  std::fprintf(stderr, "[%s] %s\n", kNames[i], msg);
}

}  // namespace log

namespace random {

uint32_t seed() {
  std::random_device rd;
  return rd();
}

}  // namespace random

namespace app {

bool quitRequested() { return g.quit; }
void heartbeat() {}
void setActivity(const char*) {}
LastReset lastReset() { return LastReset{ResetCause::None, {}}; }

}  // namespace app

namespace power {

PowerState read() { return PowerState{false, 0, 0, false, false}; }  // kein Akku am PC
void shutdownReady() {}

}  // namespace power
}  // namespace pd::hal
