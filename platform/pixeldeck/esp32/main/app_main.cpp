// ESP32-Einstieg (SPEC 14): Hardware vorbereiten, OS-Hauptschleife auf Kern 1 starten.
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>

#include "esp_platform.h"
#include "pd/config.h"
#include "pd/os/runtime.h"

namespace pd::esp {
TaskHandle_t g_mainTask = nullptr;
}

namespace {

const char* TAG = "main";

void mainTask(void*) {
  ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));  // nur die Hauptschleife wird überwacht
  pd::os::RunOptions opts;
  opts.tickWarnUs = 1000000 / PD_TICK_HZ;  // Warnung erst, wenn ein Tick das Budget sprengt
  const int rc = pd::os::run(opts);
  ESP_LOGE(TAG, "Hauptschleife beendet (%d)", rc);
  esp_task_wdt_delete(nullptr);
  vTaskDelete(nullptr);
}

void logHeap(const char* when) {
  ESP_LOGI(TAG, "Heap %s: frei %u, größter Block %u, DMA frei %u", when,
           unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)));
}

}  // namespace

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS wird neu angelegt (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  pd::esp::resetInfoInit();
  pd::esp::seedInit();
  logHeap("vor Display");
  if (!pd::esp::displayHwInit()) ESP_LOGE(TAG, "Display nicht verfügbar");
  logHeap("nach Display");
  pd::esp::linkStart();

  xTaskCreatePinnedToCore(mainTask, "pd_main", PD_MAIN_STACK_SIZE, nullptr, 5,
                          &pd::esp::g_mainTask, 1);
}
