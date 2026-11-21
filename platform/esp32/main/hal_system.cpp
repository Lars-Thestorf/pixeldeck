// HAL für Zeit, Log, Zufall, Storage (NVS) und Programmlauf/Watchdog (SPEC 11, 14.2).
#include <bootloader_random.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <nvs.h>

#include <cstdio>
#include <cstring>

#include "esp_platform.h"
#include "pd/hal/app.h"
#include "pd/hal/log.h"
#include "pd/hal/random.h"
#include "pd/hal/storage.h"
#include "pd/hal/time.h"

namespace {

const char* TAG = "pd";

// Übersteht Watchdog-Reset und Panic, nicht aber Power-on (dann ungültige Magic).
struct RtcActivity {
  uint32_t magic;
  char     gameId[13];
};
constexpr uint32_t kRtcMagic = 0x50444B31;  // "PDK1"
RTC_NOINIT_ATTR RtcActivity g_rtc;

pd::hal::app::LastReset g_lastReset{};
bool g_lastResetTaken = false;
uint32_t g_entropy[8];

}  // namespace

namespace pd::esp {

void resetInfoInit() {
  using pd::hal::app::ResetCause;
  const esp_reset_reason_t r = esp_reset_reason();
  ResetCause cause = ResetCause::None;
  switch (r) {
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
      cause = ResetCause::Hang;
      break;
    case ESP_RST_PANIC:
      cause = ResetCause::Crash;
      break;
    default:
      break;
  }
  g_lastReset = {};
  if (cause != ResetCause::None) {
    g_lastReset.cause = cause;
    if (g_rtc.magic == kRtcMagic) {
      std::memcpy(g_lastReset.gameId, g_rtc.gameId, sizeof g_lastReset.gameId);
      g_lastReset.gameId[sizeof g_lastReset.gameId - 1] = 0;
    }
  }
  ESP_LOGI(TAG, "Neustartgrund %d, letzte Aktivität '%s'", int(r),
           g_rtc.magic == kRtcMagic ? g_lastReset.gameId : "?");
  g_rtc.magic = kRtcMagic;
  g_rtc.gameId[0] = 0;
}

void seedInit() {
  // Ohne WLAN liefert esp_random() nur mit dieser Quelle echte Entropie.
  // Nutzt I2S0/ADC; deshalb vor dem Start der HUB75-DMA aufrufen.
  bootloader_random_enable();
  for (uint32_t& w : g_entropy) w = esp_random();
  bootloader_random_disable();
}

}  // namespace pd::esp

namespace pd::hal {

namespace time {

uint64_t micros() { return uint64_t(esp_timer_get_time()); }

void sleepUntil(uint64_t target) {
  // grob per vTaskDelay (1-ms-Tick), den Rest aktiv warten
  for (;;) {
    const uint64_t now = micros();
    if (now >= target) return;
    const uint64_t left = target - now;
    if (left > 2000) vTaskDelay(pdMS_TO_TICKS((left - 1000) / 1000));
  }
}

}  // namespace time

namespace log {

void write(Level lvl, const char* msg) {
  switch (lvl) {
    case Level::Debug: ESP_LOGD(TAG, "%s", msg); break;
    case Level::Info:  ESP_LOGI(TAG, "%s", msg); break;
    case Level::Warn:  ESP_LOGW(TAG, "%s", msg); break;
    default:           ESP_LOGE(TAG, "%s", msg); break;
  }
}

}  // namespace log

namespace random {

uint32_t seed() {
  static uint32_t n = 0;
  uint32_t v = esp_random() ^ g_entropy[n++ % 8];
  v ^= uint32_t(esp_timer_get_time());
  return v;
}

}  // namespace random

namespace app {

bool quitRequested() { return false; }

void heartbeat() {
  esp_task_wdt_reset();
  pd::esp::devMainTaskHook();
}

void setActivity(const char* gameId) {
  g_rtc.magic = kRtcMagic;
  std::snprintf(g_rtc.gameId, sizeof g_rtc.gameId, "%s", gameId ? gameId : "");
}

LastReset lastReset() {
  if (g_lastResetTaken) return LastReset{ResetCause::None, {}};
  g_lastResetTaken = true;
  return g_lastReset;
}

}  // namespace app

namespace storage {

bool load(const char* ns, const char* key, void* buf, size_t len, size_t* outLen) {
  if (outLen) *outLen = 0;
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
  size_t size = 0;
  bool ok = nvs_get_blob(h, key, nullptr, &size) == ESP_OK;
  if (ok && outLen) *outLen = size;
  // Bei abweichender Größe nur melden; der Aufrufer verwirft dann (wie am PC).
  if (ok && size <= len) ok = nvs_get_blob(h, key, buf, &size) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool save(const char* ns, const char* key, const void* buf, size_t len) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open(%s): %s", ns, esp_err_to_name(e));
    return false;
  }
  e = nvs_set_blob(h, key, buf, len);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  if (e != ESP_OK) ESP_LOGE(TAG, "NVS %s/%s: %s", ns, key, esp_err_to_name(e));
  return e == ESP_OK;
}

bool erase(const char* ns, const char* key) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_erase_key(h, key) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace storage
}  // namespace pd::hal
