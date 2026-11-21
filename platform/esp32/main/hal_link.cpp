// Link zum xmega über UART0-RX: Statuspakete (Tasten, Sticks, Akku) und, im Dev-Build,
// Textbefehle vom PC. Der xmega leitet USB-CDC auf dieselbe Leitung durch.
//
// Paket (10 Byte, alle 1024 Durchläufe der xmega-Hauptschleife):
//   u16 buttons (LE) | u8 lx | u8 ly | u8 rx | u8 ry | u16 battery ADC (LE) | crcH | crcL
// CRC: xmega-CRC-Modul (CRC16-CCITT), geprüft wie im HMatrix-Projekt.
#include <driver/uart.h>
#include <esp_crc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_platform.h"
#include "pd/hal/input.h"
#include "pd/hal/power.h"
#include "pd/input.h"

#ifndef PD_DEV
#define PD_DEV 1
#endif

namespace {

const char* TAG = "link";

// xmega-Bitbelegung (esp32/impl/impl/xmegaComm/xmegaComm.h im HMatrix-Projekt)
enum : uint16_t {
  X_A = 1 << 0, X_B = 1 << 1, X_X = 1 << 2, X_Y = 1 << 3, X_L = 1 << 4, X_R = 1 << 5,
  X_SELECT = 1 << 6, X_START = 1 << 7, X_DL = 1 << 8, X_DU = 1 << 9, X_DR = 1 << 10,
  X_DD = 1 << 11, X_JL = 1 << 12, X_JR = 1 << 13, X_CRG = 1 << 14, X_PWR = 1 << 15,
};

struct BitMap { uint16_t x; pd::Button b; };
constexpr BitMap kButtonMap[] = {
    {X_A, pd::Button::A},       {X_B, pd::Button::B},          {X_X, pd::Button::X},
    {X_Y, pd::Button::Y},       {X_L, pd::Button::L},          {X_R, pd::Button::R},
    {X_START, pd::Button::Start}, {X_SELECT, pd::Button::Select},
    {X_DU, pd::Button::Up},     {X_DD, pd::Button::Down},      {X_DL, pd::Button::Left},
    {X_DR, pd::Button::Right},  {X_JL, pd::Button::L3},        {X_JR, pd::Button::R3},
};

constexpr int kPacketLen = 10;
constexpr int64_t kLinkTimeoutUs = 500 * 1000;  // [Annahme] ohne Paket gilt der Link als weg
constexpr int kDeadzone = 16;                   // [Annahme] Stick-Totzone (Rohwert)
constexpr int kStickCenter = 128;               // [Annahme] Ruhelage, am Gerät messen

struct Packet {
  uint16_t buttons;
  uint8_t lx, ly, rx, ry;
  uint16_t battery;
};

struct LinkState {
  Packet  pkt{};
  int64_t lastUs = 0;   // 0 = noch nie ein Paket
  uint32_t count = 0;   // gültige Pakete seit Boot
  bool    pwrArmed = false;  // PWR erst nach einmaligem Loslassen werten (Einschalt-Druck)
  // Dev-Kanal: injizierte Eingaben
  uint16_t injButtons = 0;
  int64_t  injButtonsUntil = 0;
  int8_t   injAxes[4] = {};
  int64_t  injAxesUntil = 0;
};

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
LinkState g_link;

// --- Pakete -----------------------------------------------------------------------

uint8_t g_win[kPacketLen];
int g_winFill = 0;
bool g_rawLog = false;
uint32_t g_rateCount = 0;
int64_t g_rateStart = 0;
uint32_t g_ratePerSec = 0;
uint32_t g_rxBytes = 0;     // alle Bytes auf UART0-RX seit Boot
uint32_t g_rxOverflow = 0;  // UART-FIFO-/Puffer-Überläufe

void onPacket(const uint8_t* b) {
  Packet p;
  p.buttons = uint16_t(b[0] | (b[1] << 8));
  p.lx = b[2]; p.ly = b[3]; p.rx = b[4]; p.ry = b[5];
  p.battery = uint16_t(b[6] | (b[7] << 8));
  const int64_t now = esp_timer_get_time();

  portENTER_CRITICAL(&g_mux);
  const Packet prev = g_link.pkt;
  const bool first = g_link.lastUs == 0;
  g_link.pkt = p;
  g_link.lastUs = now;
  ++g_link.count;
  if (!(p.buttons & X_PWR)) g_link.pwrArmed = true;
  const uint32_t count = g_link.count;
  portEXIT_CRITICAL(&g_mux);

  if (first) ESP_LOGI(TAG, "xmega-Link steht (Akku-ADC %u)", p.battery);
  if ((prev.buttons ^ p.buttons) & X_PWR)  // Messung für SPEC 14.3
    ESP_LOGI(TAG, "PWR %s (Paket %lu, t=%lld ms)", (p.buttons & X_PWR) ? "gedrückt" : "losgelassen",
             (unsigned long)count, (long long)(now / 1000));
  if (g_rawLog && (first || prev.buttons != p.buttons || std::abs(prev.lx - p.lx) > 2 ||
                   std::abs(prev.ly - p.ly) > 2 || std::abs(prev.rx - p.rx) > 2 ||
                   std::abs(prev.ry - p.ry) > 2))
    printf("@@RAW buttons=%04x lx=%u ly=%u rx=%u ry=%u bat=%u\n", p.buttons, p.lx, p.ly, p.rx,
           p.ry, p.battery);

  ++g_rateCount;
  if (g_rateStart == 0) g_rateStart = now;
  if (now - g_rateStart >= 1000000) {
    g_ratePerSec = uint32_t(int64_t(g_rateCount) * 1000000 / (now - g_rateStart));
    g_rateCount = 0;
    g_rateStart = now;
  }
}

// Schiebefenster über den Byte-Strom: passt die CRC über die letzten 10 Byte, ist es
// ein Paket. Hält Resync nach Störungen und verträgt eingestreute Textbefehle.
void feedPacket(uint8_t c) {
  if (g_winFill < kPacketLen) {
    g_win[g_winFill++] = c;
  } else {
    std::memmove(g_win, g_win + 1, kPacketLen - 1);
    g_win[kPacketLen - 1] = c;
  }
  if (g_winFill < kPacketLen) return;
  const uint16_t crc = uint16_t(~esp_crc16_be(0, g_win, kPacketLen));
  if (crc == 0) {
    onPacket(g_win);
    g_winFill = 0;
  }
}

// --- Dev-Kanal ----------------------------------------------------------------------

#if PD_DEV
uint16_t g_frame[64 * 32];
portMUX_TYPE g_frameMux = portMUX_INITIALIZER_UNLOCKED;
volatile int g_mainCmd = 0;  // 1 = hang, 2 = crash
char g_line[64];
int g_lineLen = -1;  // -1 = keine Zeile in Arbeit

void printBase64(const uint8_t* d, size_t n) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char out[5] = {};
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(d[i]) << 16 | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0) |
                       (i + 2 < n ? d[i + 2] : 0);
    out[0] = k[(v >> 18) & 63];
    out[1] = k[(v >> 12) & 63];
    out[2] = i + 1 < n ? k[(v >> 6) & 63] : '=';
    out[3] = i + 2 < n ? k[v & 63] : '=';
    fputs(out, stdout);
  }
}

bool parseButtons(const char* s, uint16_t& out) {
  static const struct { const char* n; pd::Button b; } kNames[] = {
      {"a", pd::Button::A}, {"b", pd::Button::B}, {"x", pd::Button::X}, {"y", pd::Button::Y},
      {"l", pd::Button::L}, {"r", pd::Button::R}, {"start", pd::Button::Start},
      {"select", pd::Button::Select}, {"up", pd::Button::Up}, {"down", pd::Button::Down},
      {"left", pd::Button::Left}, {"right", pd::Button::Right}, {"l3", pd::Button::L3},
      {"r3", pd::Button::R3}};
  out = 0;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%s", s);
  for (char* t = std::strtok(buf, ","); t; t = std::strtok(nullptr, ",")) {
    bool ok = false;
    for (const auto& e : kNames)
      if (!std::strcmp(t, e.n)) { out |= uint16_t(e.b); ok = true; }
    if (!ok) return false;
  }
  return out != 0;
}

void cmdStat() {
  portENTER_CRITICAL(&g_mux);
  const LinkState s = g_link;
  portEXIT_CRITICAL(&g_mux);
  const int64_t now = esp_timer_get_time();
  printf("@@STAT heap=%u heap_min=%u largest=%u dma=%u dma_largest=%u stack_free=%u "
         "present_us=%lu present_max_us=%lu xmega_pkts=%lu xmega_rate=%lu/s rx_bytes=%lu rx_overflow=%lu link_age_ms=%lld "
         "buttons=%04x lx=%u ly=%u rx=%u ry=%u bat=%u\n",
         unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
         unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
         unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
         unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)),
         unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
         pd::esp::g_mainTask ? unsigned(uxTaskGetStackHighWaterMark(pd::esp::g_mainTask)) : 0u,
         (unsigned long)pd::esp::g_presentUsLast, (unsigned long)pd::esp::g_presentUsMax,
         (unsigned long)s.count, (unsigned long)g_ratePerSec, (unsigned long)g_rxBytes,
         (unsigned long)g_rxOverflow,
         s.lastUs ? (long long)((now - s.lastUs) / 1000) : -1LL, s.pkt.buttons, s.pkt.lx,
         s.pkt.ly, s.pkt.rx, s.pkt.ry, s.pkt.battery);
}

void runCommand(char* line) {
  char* argv[6] = {};
  int argc = 0;
  for (char* t = std::strtok(line, " "); t && argc < 6; t = std::strtok(nullptr, " "))
    argv[argc++] = t;
  if (argc == 0) return;
  const char* cmd = argv[0];
  const int64_t now = esp_timer_get_time();

  if (!std::strcmp(cmd, "fb")) {
    static uint16_t copy[64 * 32];
    portENTER_CRITICAL(&g_frameMux);
    std::memcpy(copy, g_frame, sizeof copy);
    portEXIT_CRITICAL(&g_frameMux);
    fputs("@@FB ", stdout);
    printBase64(reinterpret_cast<const uint8_t*>(copy), sizeof copy);
    fputs("\n", stdout);
  } else if (!std::strcmp(cmd, "press") && argc >= 2) {
    uint16_t b = 0;
    if (!parseButtons(argv[1], b)) { printf("@@ERR Tasten: %s\n", argv[1]); return; }
    const int ms = argc >= 3 ? std::atoi(argv[2]) : 120;
    portENTER_CRITICAL(&g_mux);
    g_link.injButtons = b;
    g_link.injButtonsUntil = now + int64_t(ms) * 1000;
    portEXIT_CRITICAL(&g_mux);
    printf("@@OK press %04x %d\n", b, ms);
  } else if (!std::strcmp(cmd, "stick") && argc >= 5) {
    const int ms = argc >= 6 ? std::atoi(argv[5]) : 300;
    portENTER_CRITICAL(&g_mux);
    for (int i = 0; i < 4; ++i) {
      const int v = std::atoi(argv[1 + i]);
      g_link.injAxes[i] = int8_t(v < -127 ? -127 : v > 127 ? 127 : v);
    }
    g_link.injAxesUntil = now + int64_t(ms) * 1000;
    portEXIT_CRITICAL(&g_mux);
    printf("@@OK stick\n");
  } else if (!std::strcmp(cmd, "stat")) {
    cmdStat();
  } else if (!std::strcmp(cmd, "raw")) {
    g_rawLog = argc < 2 || std::strcmp(argv[1], "off") != 0;
    printf("@@OK raw %s\n", g_rawLog ? "on" : "off");
  } else if (!std::strcmp(cmd, "hang")) {
    g_mainCmd = 1;
    printf("@@OK hang\n");
  } else if (!std::strcmp(cmd, "crash")) {
    g_mainCmd = 2;
    printf("@@OK crash\n");
  } else if (!std::strcmp(cmd, "reset")) {
    printf("@@OK reset\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
  } else {
    printf("@@ERR unbekannt: %s\n", cmd);
  }
}

// Textzeile "!befehl args\n". Nicht druckbare Bytes (xmega-Pakete) brechen die Zeile ab.
void feedText(uint8_t c) {
  if (c == '!') { g_lineLen = 0; return; }
  if (g_lineLen < 0) return;
  if (c == '\n' || c == '\r') {
    g_line[g_lineLen] = 0;
    g_lineLen = -1;
    runCommand(g_line);
    return;
  }
  if (c < 0x20 || c > 0x7E || g_lineLen >= int(sizeof g_line) - 1) { g_lineLen = -1; return; }
  g_line[g_lineLen++] = char(c);
}
#endif  // PD_DEV

void rxTask(void*) {
  uint8_t buf[128];
  int64_t rateLogAt = esp_timer_get_time() + 5000000;
  for (;;) {
    const int n = uart_read_bytes(UART_NUM_0, buf, sizeof buf, pdMS_TO_TICKS(20));
    if (n > 0) g_rxBytes += uint32_t(n);
    size_t pending = 0;
    if (uart_get_buffered_data_len(UART_NUM_0, &pending) == ESP_OK && pending > 900) ++g_rxOverflow;
    for (int i = 0; i < n; ++i) {
      feedPacket(buf[i]);
#if PD_DEV
      feedText(buf[i]);
#endif
    }
    if (rateLogAt && esp_timer_get_time() >= rateLogAt) {
      rateLogAt = 0;
      ESP_LOGI(TAG, "xmega: %lu Pakete und %lu Bytes in 5 s", (unsigned long)g_link.count,
               (unsigned long)g_rxBytes);
    }
  }
}

int8_t stickAxis(uint8_t raw) {
  int v = int(raw) - kStickCenter;
  const int a = v < 0 ? -v : v;
  if (a <= kDeadzone) return 0;
  int s = (a - kDeadzone) * 127 / (127 - kDeadzone);
  if (s > 127) s = 127;
  return int8_t(v < 0 ? -s : s);
}

}  // namespace

namespace pd::esp {

void linkStart() {
  uart_config_t cfg = {};
  cfg.baud_rate = 115200;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  // Nur RX-Puffer: TX schreibt weiter die Konsole (printf/ESP_LOG)
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
  xTaskCreatePinnedToCore(rxTask, "pd_link", 4096, nullptr, 6, nullptr, 0);
  ESP_LOGI(TAG, "UART0-RX gestartet%s", PD_DEV ? ", Dev-Kanal aktiv" : "");
}

void devSnapshotFrame(const uint16_t* fb) {
#if PD_DEV
  portENTER_CRITICAL(&g_frameMux);
  std::memcpy(g_frame, fb, sizeof g_frame);
  portEXIT_CRITICAL(&g_frameMux);
#else
  (void)fb;
#endif
}

void devMainTaskHook() {
#if PD_DEV
  const int c = g_mainCmd;
  if (!c) return;
  g_mainCmd = 0;
  if (c == 1) {
    ESP_LOGW(TAG, "Test-Hänger in der Hauptschleife");
    for (;;) {}  // Watchdog muss nach 5 s zuschlagen
  }
  ESP_LOGW(TAG, "Test-Absturz in der Hauptschleife");
  volatile int* p = nullptr;
  *p = 42;
#endif
}

}  // namespace pd::esp

namespace pd::hal::input {

size_t poll(Device* out, size_t max) {
  if (max == 0) return 0;
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&g_mux);
  const LinkState s = g_link;
  portEXIT_CRITICAL(&g_mux);

  Device& d = out[0];
  d.id = 1;
  d.type = DeviceType::Builtin;
  std::snprintf(d.name, sizeof d.name, "KONSOLE");
  const bool linkUp = s.lastUs != 0 && now - s.lastUs < kLinkTimeoutUs;
  const bool injecting = now < s.injButtonsUntil || now < s.injAxesUntil;
  d.connected = linkUp || injecting;

  uint16_t b = 0;
  RawInput st{0, 0, 0, 0, 0};
  if (linkUp) {
    for (const BitMap& m : kButtonMap)
      if (s.pkt.buttons & m.x) b |= uint16_t(m.b);
    st.lx = stickAxis(s.pkt.lx);
    st.ly = stickAxis(s.pkt.ly);
    st.rx = stickAxis(s.pkt.rx);
    st.ry = stickAxis(s.pkt.ry);
  }
  if (now < s.injButtonsUntil) b |= s.injButtons;
  if (now < s.injAxesUntil) {
    st.lx = s.injAxes[0]; st.ly = s.injAxes[1]; st.rx = s.injAxes[2]; st.ry = s.injAxes[3];
  }
  st.buttons = b;
  d.state = st;
  return 1;
}

void rumble(uint32_t, uint16_t) {}  // keine Vibration in der Konsole

}  // namespace pd::hal::input

namespace pd::hal::power {

PowerState read() {
  portENTER_CRITICAL(&g_mux);
  const LinkState s = g_link;
  portEXIT_CRITICAL(&g_mux);
  PowerState p{};
  if (s.lastUs == 0) return p;
  // HMatrix-Formel: ADC 4095 = 4125 mV, 3300 mV leer, 4125 mV voll [Annahme, nicht kalibriert]
  p.available = true;
  p.millivolts = uint16_t(uint32_t(s.pkt.battery) * 4125u / 4095u);
  const int pct = (int(p.millivolts) - 3300) * 100 / 825;
  p.percent = uint8_t(pct < 0 ? 0 : pct > 100 ? 100 : pct);
  p.charging = (s.pkt.buttons & X_CRG) != 0;
  p.shutdownPending = s.pwrArmed && (s.pkt.buttons & X_PWR) != 0;
  return p;
}

void shutdownReady() {
  // Der xmega schaltet selbst ab (Taste loslassen); kein Rückkanal vorhanden.
  ESP_LOGI(TAG, "shutdownReady, t=%lld ms", (long long)(esp_timer_get_time() / 1000));
}

}  // namespace pd::hal::power
