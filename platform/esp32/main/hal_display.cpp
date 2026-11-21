// Display-HAL: 64x32 HUB75 per I2S-DMA (Lib ESP32-HUB75-MatrixPanel-DMA 3.1.0, MIT).
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>
#include <cstring>

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "esp_platform.h"
#include "pd/hal/display.h"

#ifndef PD_PANEL_GAMMA
#define PD_PANEL_GAMMA 2.2f  // am Gerät bestätigt (M3)
#endif

namespace {

const char* TAG = "display";

// Pinbelegung der Konsole (aus dem HMatrix-Projekt übernommen)
constexpr HUB75_I2S_CFG::i2s_pins kPins = {
    /*r1*/ 32, /*g1*/ 33, /*b1*/ 27, /*r2*/ 14, /*g2*/ 12, /*b2*/ 13,
    /*a*/ 2, /*b*/ 0, /*c*/ 4, /*d*/ 16, /*e*/ -1,
    /*lat*/ 5, /*oe*/ 18, /*clk*/ 17};

constexpr int W = 64, H = 32;

MatrixPanel_I2S_DMA* g_panel = nullptr;
uint8_t g_lut5[32], g_lut6[64];

inline void pin(int8_t p, int level) { gpio_set_level(gpio_num_t(p), level); }
inline void clk() { pin(kPins.clk, 1); pin(kPins.clk, 0); }

// FM6126A-Register per Bitbang setzen, bevor die DMA-Ausgabe startet.
// Werte aus dem HMatrix-Projekt: REG1 komplett 1, REG2 komplett 0. Die Upstream-
// Werte (REG1 = 0000011111100000, REG2 mit Bit 9) ergaben auf diesem Panel einen Rotstich.
void fm6126aInit() {
  static constexpr bool REG1[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  static constexpr bool REG2[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const int8_t data[] = {kPins.r1, kPins.g1, kPins.b1, kPins.r2, kPins.g2, kPins.b2};
  const int8_t ctrl[] = {kPins.r1, kPins.g1, kPins.b1, kPins.r2, kPins.g2, kPins.b2,
                         kPins.clk, kPins.lat, kPins.oe};
  for (int8_t p : ctrl) {
    gpio_reset_pin(gpio_num_t(p));
    gpio_set_direction(gpio_num_t(p), GPIO_MODE_OUTPUT);
    pin(p, 0);
  }
  pin(kPins.oe, 1);  // Ausgabe aus

  for (int l = 0; l < W; ++l) {  // REG1: LAT die letzten 11 Takte
    for (int8_t p : data) pin(p, REG1[l % 16]);
    if (l > W - 12) pin(kPins.lat, 1);
    clk();
  }
  pin(kPins.lat, 0);
  for (int l = 0; l < W; ++l) {  // REG2: LAT die letzten 12 Takte
    for (int8_t p : data) pin(p, REG2[l % 16]);
    if (l > W - 13) pin(kPins.lat, 1);
    clk();
  }
  pin(kPins.lat, 0);
  for (int8_t p : data) pin(p, 0);  // Schieberegister leeren
  for (int l = 0; l < W; ++l) clk();
  pin(kPins.lat, 1);
  clk();
  pin(kPins.lat, 0);
  pin(kPins.oe, 0);
  clk();
}

void buildGamma() {
  for (int i = 0; i < 32; ++i)
    g_lut5[i] = uint8_t(std::lround(255.0f * std::pow(i / 31.0f, PD_PANEL_GAMMA)));
  for (int i = 0; i < 64; ++i)
    g_lut6[i] = uint8_t(std::lround(255.0f * std::pow(i / 63.0f, PD_PANEL_GAMMA)));
}

}  // namespace

namespace pd::esp {

bool displayHwInit() {
  if (g_panel) return true;
  buildGamma();
  fm6126aInit();

  HUB75_I2S_CFG cfg(W, H, 1, kPins);
  cfg.driver = HUB75_I2S_CFG::SHIFTREG;  // FM6126A-Init schon erledigt (fm6126aInit)
  cfg.double_buff = true;
  cfg.i2sspeed = HUB75_I2S_CFG::HZ_10M;
  cfg.clkphase = false;
  g_panel = new MatrixPanel_I2S_DMA(cfg);
  if (!g_panel->begin()) {
    ESP_LOGE(TAG, "HUB75-Init fehlgeschlagen");
    delete g_panel;
    g_panel = nullptr;
    return false;
  }
  g_panel->setBrightness8(128);
  g_panel->clearScreen();
  g_panel->flipDMABuffer();
  g_panel->clearScreen();
  return true;
}

uint32_t g_presentUsLast = 0, g_presentUsMax = 0;

}  // namespace pd::esp

namespace pd::hal::display {

void init() { pd::esp::displayHwInit(); }

void present(const uint16_t* fb) {
  pd::esp::devSnapshotFrame(fb);
  if (!g_panel) return;
  const int64_t t0 = esp_timer_get_time();
  for (int y = 0; y < H; ++y) {
    const uint16_t* row = fb + y * W;
    for (int x = 0; x < W; ++x) {
      const uint16_t c = row[x];
      g_panel->drawPixelRGB888(int16_t(x), int16_t(y), g_lut5[c >> 11], g_lut6[(c >> 5) & 63],
                               g_lut5[c & 31]);
    }
  }
  g_panel->flipDMABuffer();
  const uint32_t dt = uint32_t(esp_timer_get_time() - t0);
  pd::esp::g_presentUsLast = dt;
  if (dt > pd::esp::g_presentUsMax) pd::esp::g_presentUsMax = dt;
}

void setBrightness(uint8_t level) {
  if (g_panel) g_panel->setBrightness8(level);
}

}  // namespace pd::hal::display
