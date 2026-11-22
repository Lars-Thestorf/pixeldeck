// WebPad-Transport auf dem ESP32 (SPEC 13): eigener Access Point, Captive Portal (DNS auf die
// eigene IP, unbekannte URLs per 302 auf die Seite), HTTP und WebSocket mit esp_http_server.
//
// Ereignisse aus dem HTTP-Task gehen über eine FreeRTOS-Queue an die Hauptschleife
// (hal::net::poll). Gesendet wird per httpd_queue_work im Kontext des HTTP-Tasks.
// stop() baut WLAN komplett ab (esp_wifi_deinit), damit der RAM wieder frei ist.
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pd/hal/net.h"

namespace pd::hal::net {
namespace {

const char* TAG = "net";

constexpr int kMaxPages = 4;
constexpr int kMaxStations = 4;        // [Annahme] 4 Handys; mehr Slots gibt es nicht
constexpr uint8_t kChannel = 6;        // [Annahme] fester Kanal

struct Page {
  char path[32];
  const uint8_t* gz;
  size_t len;
  char mime[48];
};
Page g_pages[kMaxPages];
int g_pageCount = 0;

QueueHandle_t g_queue = nullptr;
httpd_handle_t g_server = nullptr;
esp_netif_t* g_ap = nullptr;
bool g_running = false;
bool g_loopInit = false;
char g_host[24] = "192.168.77.1";
uint32_t g_ip = 0;                     // Netzwerk-Byte-Reihenfolge

TaskHandle_t g_dnsTask = nullptr;
volatile bool g_dnsStop = false;

// --- Diagnose (Aussetzer mit mehreren Handys, SPEC 18) -------------------------------
// Pro WebSocket: Frames, größte Lücke zwischen zwei Frames. Läuft im HTTP-Task, gelesen und
// ausgegeben aus poll() (Hauptschleife); kleine Ungenauigkeiten durch fehlende Sperre sind egal.
struct Diag {
  int      fd = -1;
  int64_t  lastUs = 0;
  uint32_t frames = 0;        // seit der letzten Statistik
  uint32_t maxGapMs = 0;      // seit der letzten Statistik
  bool     started = false;   // erster Frame empfangen
};
constexpr int kDiag = 12;
Diag g_diag[kDiag];
volatile uint32_t g_queueMax = 0;
int64_t g_statUs = 0;

Diag* diag(int fd, bool create) {
  for (Diag& d : g_diag)
    if (d.fd == fd) return &d;
  if (!create) return nullptr;
  for (Diag& d : g_diag)
    if (d.fd < 0) { d = Diag{}; d.fd = fd; d.lastUs = esp_timer_get_time(); return &d; }
  return nullptr;
}

// CPU-Last je Kern aus den FreeRTOS-Laufzeitstatistiken (Idle-Tasks), für die 5-s-Statistik
struct RunPrev { TaskHandle_t h; uint32_t rt; };
RunPrev g_runPrev[32];
int g_runPrevN = 0;
int64_t g_runPrevUs = 0;

void cpuLoad(char* out, size_t len) {
  TaskStatus_t st[32];
  uint32_t total = 0;
  const UBaseType_t n = uxTaskGetSystemState(st, 32, &total);
  const int64_t now = esp_timer_get_time();
  const uint32_t span = uint32_t(now - g_runPrevUs);
  uint32_t idle[2] = {0, 0};
  const char* topName = "-";
  uint32_t topDelta = 0;
  for (UBaseType_t i = 0; i < n; ++i) {
    uint32_t prev = 0;
    bool known = false;
    for (int k = 0; k < g_runPrevN; ++k)
      if (g_runPrev[k].h == st[i].xHandle) { prev = g_runPrev[k].rt; known = true; }
    const uint32_t d = known ? st[i].ulRunTimeCounter - prev : 0;
    if (!std::strncmp(st[i].pcTaskName, "IDLE", 4)) {
      const int core = st[i].pcTaskName[4] == '1' ? 1 : 0;
      idle[core] += d;
    } else if (d > topDelta) {
      topDelta = d;
      topName = st[i].pcTaskName;
    }
  }
  if (g_runPrevUs && span > 0)
    std::snprintf(out, len, "CPU0 %u%% CPU1 %u%% (meist %s %u%%)",
                  unsigned(100 - std::min<uint64_t>(100, uint64_t(idle[0]) * 100 / span)),
                  unsigned(100 - std::min<uint64_t>(100, uint64_t(idle[1]) * 100 / span)), topName,
                  unsigned(uint64_t(topDelta) * 100 / span));
  else
    std::snprintf(out, len, "CPU ?");
  g_runPrevN = 0;
  for (UBaseType_t i = 0; i < n && g_runPrevN < 32; ++i)
    g_runPrev[g_runPrevN++] = {st[i].xHandle, st[i].ulRunTimeCounter};
  g_runPrevUs = now;
}

void push(EventType t, int fd, const uint8_t* d = nullptr, size_t n = 0, TickType_t wait = 0) {
  if (!g_queue) return;
  Event e{};
  e.type = t;
  e.client = ClientId(fd);
  e.len = uint8_t(n);
  if (n) std::memcpy(e.data, d, n);
  const uint32_t waiting = uint32_t(uxQueueMessagesWaiting(g_queue)) + 1;
  if (waiting > g_queueMax) g_queueMax = waiting;
  if (xQueueSend(g_queue, &e, wait) != pdTRUE) ESP_LOGW(TAG, "Warteschlange voll, Ereignis verworfen");
}

// --- HTTP -------------------------------------------------------------------------

// Normale HTTP-Verbindungen nach der Antwort schließen. Handys halten sonst nach Seitenaufruf
// und Captive-Portal-Prüfungen Keep-Alive-Sockets offen; mit mehreren Handys war die
// lwIP-Socket-Tabelle voll (accept: ENFILE) und weitere Handys kamen nicht mehr durch.
void closeAfter(httpd_req_t* req) { httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req)); }

esp_err_t pageHandler(httpd_req_t* req) {
  const Page* p = static_cast<const Page*>(req->user_ctx);
  httpd_resp_set_type(req, p->mime);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  const esp_err_t err = httpd_resp_send(req, reinterpret_cast<const char*>(p->gz), ssize_t(p->len));
  closeAfter(req);
  return err;
}

// Captive Portal: jede unbekannte URL (generate_204, hotspot-detect.html, ...) auf die Seite
esp_err_t redirect(httpd_req_t* req, httpd_err_code_t) {
  char loc[40];
  std::snprintf(loc, sizeof loc, "http://%s/", g_host);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", loc);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  const esp_err_t err = httpd_resp_send(req, nullptr, 0);
  closeAfter(req);
  return err;
}

esp_err_t wsHandler(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) {  // Handshake abgeschlossen
    diag(fd, true);
    const int one = 1;  // kleine Nachrichten sofort senden (kein Nagle)
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    ESP_LOGI(TAG, "WebSocket fd %d offen", fd);
    push(EventType::Open, fd, nullptr, 0, pdMS_TO_TICKS(20));
    return ESP_OK;
  }
  const int64_t t0 = esp_timer_get_time();
  if (Diag* dg = diag(fd, true)) {
    const uint32_t gap = uint32_t((t0 - dg->lastUs) / 1000);
    if (gap > 250 && dg->started) ESP_LOGI(TAG, "fd %d: Lücke %u ms", fd, unsigned(gap));
    if (dg->started && gap > dg->maxGapMs) dg->maxGapMs = gap;
    dg->started = true;
    dg->lastUs = t0;
    ++dg->frames;
  }
  httpd_ws_frame_t f{};
  esp_err_t err = httpd_ws_recv_frame(req, &f, 0);  // nur Länge
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "fd %d: Kopf lesen: %s", fd, esp_err_to_name(err));
    return err;
  }
  uint8_t buf[128];
  if (f.len > sizeof buf) {                         // gibt es im Protokoll nicht: trennen
    ESP_LOGW(TAG, "fd %d: Frame zu groß (%u)", fd, unsigned(f.len));
    return ESP_FAIL;
  }
  f.payload = buf;
  if (f.len) {
    err = httpd_ws_recv_frame(req, &f, f.len);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "fd %d: Daten lesen: %s", fd, esp_err_to_name(err));
      return err;
    }
  }
  const int64_t dt = esp_timer_get_time() - t0;
  if (dt > 30000) ESP_LOGI(TAG, "fd %d: Empfang dauerte %u ms", fd, unsigned(dt / 1000));
  if (f.type == HTTPD_WS_TYPE_CLOSE) ESP_LOGI(TAG, "fd %d: Close-Frame vom Handy", fd);
  if (f.type == HTTPD_WS_TYPE_BINARY && f.len <= kMaxMessage) push(EventType::Message, fd, buf, f.len);
  return ESP_OK;
}

void onClose(httpd_handle_t, int fd) {
  if (Diag* dg = diag(fd, false)) {
    ESP_LOGI(TAG, "WebSocket fd %d geschlossen, letzter Frame vor %u ms", fd,
             unsigned((esp_timer_get_time() - dg->lastUs) / 1000));
    dg->fd = -1;
  }
  push(EventType::Close, fd, nullptr, 0, pdMS_TO_TICKS(20));  // auch HTTP-Sockets, der Core ignoriert sie
  close(fd);
}

struct SendJob {
  int fd;
  size_t len;
  uint8_t data[1];
};

void sendWork(void* arg) {
  SendJob* j = static_cast<SendJob*>(arg);
  if (g_server && httpd_ws_get_fd_info(g_server, j->fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
    // Frame selbst bauen und in einem Stück senden: httpd_ws_send_frame_async schreibt Kopf und
    // Nutzdaten getrennt, mit Nagle wartet der zweite Teil dann auf die Bestätigung des ersten.
    uint8_t buf[4 + 200];
    size_t h = 0;
    buf[h++] = 0x82;  // FIN + binär
    if (j->len < 126) {
      buf[h++] = uint8_t(j->len);
    } else {
      buf[h++] = 126;
      buf[h++] = uint8_t(j->len >> 8);
      buf[h++] = uint8_t(j->len);
    }
    if (h + j->len <= sizeof buf) {
      std::memcpy(buf + h, j->data, j->len);
      const int64_t t0 = esp_timer_get_time();
      const int sent = httpd_socket_send(g_server, j->fd, reinterpret_cast<const char*>(buf), h + j->len, 0);
      const int64_t dt = esp_timer_get_time() - t0;
      if (sent != int(h + j->len) || dt > 30000)
        ESP_LOGW(TAG, "fd %d: Senden %d/%u nach %u ms", j->fd, sent, unsigned(h + j->len), unsigned(dt / 1000));
    }
  }
  std::free(j);
}

bool startHttp() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.core_id = 0;
  // CONFIG_LWIP_MAX_SOCKETS 16 minus 3 intern (httpd) minus 1 DNS. Reicht für 4 WebSockets
  // plus kurzlebige HTTP-Verbindungen; ist es voll, schließt lru_purge die älteste.
  c.max_open_sockets = 12;
  c.lru_purge_enable = true;
  c.close_fn = onClose;
  c.max_uri_handlers = kMaxPages + 1;
  if (httpd_start(&g_server, &c) != ESP_OK) {
    g_server = nullptr;
    return false;
  }
  for (int i = 0; i < g_pageCount; ++i) {
    httpd_uri_t u{};
    u.uri = g_pages[i].path;
    u.method = HTTP_GET;
    u.handler = pageHandler;
    u.user_ctx = &g_pages[i];
    httpd_register_uri_handler(g_server, &u);
  }
  httpd_uri_t ws{};
  ws.uri = "/ws";
  ws.method = HTTP_GET;
  ws.handler = wsHandler;
  ws.is_websocket = true;
  httpd_register_uri_handler(g_server, &ws);
  httpd_register_err_handler(g_server, HTTPD_404_NOT_FOUND, redirect);
  return true;
}

// --- DNS: jede A-Anfrage mit der eigenen IP beantworten ---------------------------

void dnsTask(void*) {
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (s < 0 || bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    ESP_LOGE(TAG, "DNS: Port 53 nicht verfügbar");
    if (s >= 0) close(s);
    g_dnsTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  timeval tv{0, 200000};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  uint8_t buf[512];
  while (!g_dnsStop) {
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    const int n = recvfrom(s, buf, sizeof buf - 16, 0, reinterpret_cast<sockaddr*>(&from), &fl);
    if (n < 12) continue;
    if ((buf[2] & 0x80) || (buf[2] & 0x78) || buf[4] != 0 || buf[5] != 1) continue;  // nur Standard-Query, 1 Frage
    int p = 12;
    while (p < n && buf[p] != 0) p += buf[p] + 1;  // Name
    if (p + 5 > n) continue;
    const uint16_t qtype = uint16_t(buf[p + 1] << 8 | buf[p + 2]);
    p += 5;
    buf[2] = uint8_t(0x80 | (buf[2] & 0x01));  // Antwort, RD übernehmen
    buf[3] = 0x80;                             // RA, NOERROR
    buf[6] = 0; buf[7] = qtype == 1 ? 1 : 0;   // nur A beantworten, AAAA & Co. leer
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    if (qtype == 1) {
      const uint8_t ans[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
                               uint8_t(g_ip), uint8_t(g_ip >> 8), uint8_t(g_ip >> 16), uint8_t(g_ip >> 24)};
      std::memcpy(buf + p, ans, sizeof ans);
      p += sizeof ans;
    }
    sendto(s, buf, size_t(p), 0, reinterpret_cast<sockaddr*>(&from), fl);
  }
  close(s);
  g_dnsTask = nullptr;
  vTaskDelete(nullptr);
}

void onWifiEvent(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == WIFI_EVENT_AP_STACONNECTED) {
    auto* e = static_cast<wifi_event_ap_staconnected_t*>(data);
    ESP_LOGI(TAG, "WLAN: Gerät verbunden (AID %d)", e->aid);
  } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
    auto* e = static_cast<wifi_event_ap_stadisconnected_t*>(data);
    ESP_LOGI(TAG, "WLAN: Gerät getrennt (AID %d)", e->aid);
  }
}

void logHeap(const char* when) {
  ESP_LOGI(TAG, "Heap %s: frei %u, größter Block %u", when,
           unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

}  // namespace

void serveStatic(const char* path, const uint8_t* gz, size_t len, const char* mime) {
  for (int i = 0; i < g_pageCount; ++i)
    if (!std::strcmp(g_pages[i].path, path)) {
      g_pages[i].gz = gz;
      g_pages[i].len = len;
      return;
    }
  if (g_pageCount >= kMaxPages) return;
  Page& p = g_pages[g_pageCount++];
  std::snprintf(p.path, sizeof p.path, "%s", path);
  std::snprintf(p.mime, sizeof p.mime, "%s", mime);
  p.gz = gz;
  p.len = len;
}

bool start(const char* ssid, const char* pass) {
  if (g_running) return true;
  logHeap("vor WLAN");
  if (!g_loopInit) {
    ESP_ERROR_CHECK(esp_netif_init());
    const esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr));
    g_queue = xQueueCreate(32, sizeof(Event));
    g_loopInit = true;
  }
  xQueueReset(g_queue);

  g_ap = esp_netif_create_default_wifi_ap();
  wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&wc);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
    esp_netif_destroy_default_wifi(g_ap);
    g_ap = nullptr;
    return false;
  }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  // DHCP: eigene IP als DNS anbieten, dazu die Captive-Portal-URL (RFC 8910)
  // Eigenes Netz statt des ESP-Standards 192.168.4.0/24, der oft mit vorhandenen Netzen
  // kollidiert (z. B. 192.168.0.0/21). [Annahme] 192.168.77.0/24
  esp_netif_dhcps_stop(g_ap);
  esp_netif_ip_info_t ip{};
  IP4_ADDR(&ip.ip, 192, 168, 77, 1);
  IP4_ADDR(&ip.gw, 192, 168, 77, 1);
  IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
  esp_netif_set_ip_info(g_ap, &ip);
  g_ip = ip.ip.addr;
  esp_ip4addr_ntoa(&ip.ip, g_host, sizeof g_host);
  esp_netif_dns_info_t dns{};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = g_ip;
  esp_netif_set_dns_info(g_ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offerDns = 0x02;  // OFFER_DNS
  esp_netif_dhcps_option(g_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offerDns, sizeof offerDns);
  static char uri[40];
  std::snprintf(uri, sizeof uri, "http://%s/", g_host);
  esp_netif_dhcps_option(g_ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, std::strlen(uri));
  esp_netif_dhcps_start(g_ap);

  wifi_config_t cfg{};
  std::snprintf(reinterpret_cast<char*>(cfg.ap.ssid), sizeof cfg.ap.ssid, "%s", ssid);
  cfg.ap.ssid_len = uint8_t(std::strlen(ssid));
  std::snprintf(reinterpret_cast<char*>(cfg.ap.password), sizeof cfg.ap.password, "%s", pass ? pass : "");
  cfg.ap.channel = kChannel;
  cfg.ap.max_connection = kMaxStations;
  cfg.ap.authmode = (pass && std::strlen(pass) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  cfg.ap.pmf_cfg.required = false;
  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WLAN-Start: %s", esp_err_to_name(err));
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(g_ap);
    g_ap = nullptr;
    return false;
  }
  esp_wifi_set_ps(WIFI_PS_NONE);  // geringe Latenz

  if (!startHttp()) {
    ESP_LOGE(TAG, "HTTP-Server startet nicht");
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(g_ap);
    g_ap = nullptr;
    return false;
  }
  g_dnsStop = false;
  xTaskCreatePinnedToCore(dnsTask, "pd_dns", 3072, nullptr, 4, &g_dnsTask, 0);
  g_running = true;
  ESP_LOGI(TAG, "Access Point %s, Kanal %u, http://%s/", ssid, unsigned(kChannel), g_host);
  logHeap("mit WLAN");
  return true;
}

void stop() {
  if (!g_running) return;
  g_running = false;
  g_dnsStop = true;
  for (int i = 0; i < 50 && g_dnsTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
  if (g_server) {
    httpd_stop(g_server);
    g_server = nullptr;
  }
  esp_wifi_stop();
  esp_wifi_deinit();
  if (g_ap) {
    esp_netif_destroy_default_wifi(g_ap);
    g_ap = nullptr;
  }
  if (g_queue) xQueueReset(g_queue);
  for (Diag& d : g_diag) d.fd = -1;
  logHeap("nach WLAN");
}

Info info() {
  Info i{};
  i.running = g_running;
  i.accessPoint = true;
  i.port = 80;
  std::snprintf(i.host, sizeof i.host, "%s", g_host);
  if (g_running) {
    wifi_sta_list_t list{};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) i.stations = uint8_t(list.num);
  }
  return i;
}

size_t poll(Event* out, size_t max) {
  if (!g_queue) return 0;
  // Diagnose alle 5 s, solange WebSockets offen sind
  const int64_t now = esp_timer_get_time();
  if (now - g_statUs > 5000000) {
    g_statUs = now;
    char line[160];
    int k = 0;
    for (Diag& d : g_diag) {
      if (d.fd < 0 || k > int(sizeof line) - 40) continue;
      k += std::snprintf(line + k, sizeof line - size_t(k), " fd%d:%u/%ums", d.fd,
                         unsigned(d.frames), unsigned(d.maxGapMs));
      d.frames = 0;
      d.maxGapMs = 0;
    }
    if (k > 0) {
      wifi_sta_list_t list{};
      esp_wifi_ap_get_sta_list(&list);
      char rssi[48];
      int r = 0;
      for (int i = 0; i < list.num && r < int(sizeof rssi) - 8; ++i)
        r += std::snprintf(rssi + r, sizeof rssi - size_t(r), " %d", list.sta[i].rssi);
      rssi[r] = 0;
      char cpu[64];
      cpuLoad(cpu, sizeof cpu);
      ESP_LOGI(TAG, "WS Frames/Lücke:%s | Queue max %u | RSSI%s | Heap min %u | %s", line,
               unsigned(g_queueMax), r ? rssi : " -", unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)), cpu);
      g_queueMax = 0;
    }
  }
  size_t n = 0;
  while (n < max && xQueueReceive(g_queue, &out[n], 0) == pdTRUE) ++n;
  return n;
}

bool send(ClientId c, const uint8_t* data, size_t len) {
  if (!g_server || !g_running) return false;
  SendJob* j = static_cast<SendJob*>(std::malloc(sizeof(SendJob) + len));
  if (!j) return false;
  j->fd = int(c);
  j->len = len;
  std::memcpy(j->data, data, len);
  if (httpd_queue_work(g_server, sendWork, j) != ESP_OK) {
    std::free(j);
    return false;
  }
  return true;
}

}  // namespace pd::hal::net
