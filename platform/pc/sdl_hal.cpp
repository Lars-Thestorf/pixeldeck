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
} g;

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
  if (!SDL_Init(SDL_INIT_VIDEO)) { pd::pc::logSdlError("SDL_Init"); g.quit = true; return; }
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
               "[INFO] Simulator: Esc beendet, F2 schaltet LED-Optik/Pixel. "
               "Tasten nach US-Position (QWERTZ: B liegt auf 'Y').\n");
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
  std::snprintf(d.name, sizeof d.name, "TASTATUR");
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

  // Slot 0: Tastatur, Layout 1 (SPEC 15). Scancodes = physische US-Position.
  const bool* k = SDL_GetKeyboardState(nullptr);
  using B = pd::Button;
  struct Map { SDL_Scancode sc; B b; };
  static const Map kMap[] = {
    {SDL_SCANCODE_UP, B::Up},       {SDL_SCANCODE_DOWN, B::Down},
    {SDL_SCANCODE_LEFT, B::Left},   {SDL_SCANCODE_RIGHT, B::Right},
    {SDL_SCANCODE_X, B::A},         {SDL_SCANCODE_Z, B::B},
    {SDL_SCANCODE_S, B::X},         {SDL_SCANCODE_A, B::Y},
    {SDL_SCANCODE_Q, B::L},         {SDL_SCANCODE_W, B::R},
    {SDL_SCANCODE_RETURN, B::Start}, {SDL_SCANCODE_RSHIFT, B::Select},
    {SDL_SCANCODE_U, B::L3},        {SDL_SCANCODE_KP_0, B::R3},  // [Annahme] L3/R3
  };
  uint16_t buttons = 0;
  for (const Map& m : kMap)
    if (k[m.sc]) buttons |= uint16_t(m.b);

  d.state.buttons = buttons;
  d.state.lx = pd::pc::axis(k, SDL_SCANCODE_J, SDL_SCANCODE_L);
  d.state.ly = pd::pc::axis(k, SDL_SCANCODE_I, SDL_SCANCODE_K);
  d.state.rx = pd::pc::axis(k, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6);
  d.state.ry = pd::pc::axis(k, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5);
  return 1;
}

void rumble(uint32_t, uint16_t) {}  // Tastatur: No-Op; SDL-Gamepads folgen in M4

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
