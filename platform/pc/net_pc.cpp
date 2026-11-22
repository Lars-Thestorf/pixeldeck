// WebPad-Transport am PC (SPEC 13, 15): kleiner HTTP- und WebSocket-Server im lokalen Netz.
// Kein Access Point und kein Captive Portal; das Handy öffnet http://<PC-IP>:<Port>/.
//
// Der Server läuft in einem eigenen Thread und allokiert dort nichts (feste Puffer), damit der
// Heap-Hook (heap_guard.cpp) keine Fehlalarme meldet, während Spielcode im Hauptthread läuft.
#include "pd/hal/net.h"

#include <cstdio>
#include <cstring>

#include "sim.h"

#if defined(_WIN32)

namespace pd::hal::net {
void serveStatic(const char*, const uint8_t*, size_t, const char*) {}
bool start(const char*, const char*) {
  std::fprintf(stderr, "[WARN] WebPad: unter Windows nicht umgesetzt\n");
  return false;
}
void stop() {}
Info info() { return Info{}; }
size_t poll(Event*, size_t) { return 0; }
bool send(ClientId, const uint8_t*, size_t) { return false; }
}  // namespace pd::hal::net

#else

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <mutex>
#include <thread>

namespace pd::hal::net {
namespace {

// --- SHA-1 und Base64 für den WebSocket-Handshake (RFC 6455) --------------------

struct Sha1 {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint8_t  block[64] = {};
  size_t   used = 0;
  uint64_t bits = 0;

  static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
  void compress() {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 |
             uint32_t(block[4 * i + 2]) << 8 | block[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  void add(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    bits += uint64_t(n) * 8;
    while (n--) {
      block[used++] = *p++;
      if (used == 64) { compress(); used = 0; }
    }
  }
  void finish(uint8_t out[20]) {
    const uint64_t total = bits;
    const uint8_t pad = 0x80, zero = 0;
    add(&pad, 1);
    while (used != 56) add(&zero, 1);
    for (int i = 7; i >= 0; --i) {
      const uint8_t b = uint8_t(total >> (8 * i));
      block[used++] = b;
    }
    compress();
    for (int i = 0; i < 5; ++i)
      for (int k = 0; k < 4; ++k) out[4 * i + k] = uint8_t(h[i] >> (24 - 8 * k));
  }
};

void base64(const uint8_t* in, size_t n, char* out) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) |
                       (i + 2 < n ? in[i + 2] : 0);
    out[o++] = t[(v >> 18) & 63];
    out[o++] = t[(v >> 12) & 63];
    out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? t[v & 63] : '=';
  }
  out[o] = 0;
}

// --- Zustand ----------------------------------------------------------------------

struct Page {
  char path[32];
  const uint8_t* gz;
  size_t len;
  char mime[48];
};

struct Conn {
  int      fd = -1;
  bool     ws = false;
  ClientId id = 0;
  size_t   len = 0;
  uint8_t  buf[4096];
};

constexpr int kMaxPages = 4;
constexpr int kMaxConn = 16;
constexpr size_t kQueue = 256;

Page g_pages[kMaxPages];
int g_pageCount = 0;

std::mutex g_connMu;           // Verbindungen und Schreiben auf Sockets
Conn g_conns[kMaxConn];

std::mutex g_queueMu;          // Ereignisse an den Hauptthread
Event g_queue[kQueue];
size_t g_qHead = 0, g_qCount = 0;
bool g_qWarned = false;

std::thread g_thread;
std::atomic<bool> g_stop{false};
int g_listen = -1;
bool g_running = false;
uint16_t g_port = 0;
char g_host[24] = "?";
ClientId g_nextId = 1;

void push(EventType t, ClientId c, const uint8_t* d = nullptr, size_t n = 0) {
  std::lock_guard<std::mutex> lk(g_queueMu);
  if (g_qCount == kQueue) {
    if (!g_qWarned) std::fprintf(stderr, "[WARN] WebPad: Ereignis-Warteschlange voll\n");
    g_qWarned = true;
    return;
  }
  Event& e = g_queue[(g_qHead + g_qCount++) % kQueue];
  e.type = t;
  e.client = c;
  e.len = uint8_t(n);
  if (n) std::memcpy(e.data, d, n);
}

bool sendAll(int fd, const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (n) {
    const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
    if (w <= 0) return false;
    p += w;
    n -= size_t(w);
  }
  return true;
}

bool sendFrame(int fd, uint8_t opcode, const uint8_t* d, size_t n) {
  uint8_t hdr[4];
  size_t h = 0;
  hdr[h++] = uint8_t(0x80 | opcode);
  if (n < 126) {
    hdr[h++] = uint8_t(n);
  } else {
    hdr[h++] = 126;
    hdr[h++] = uint8_t(n >> 8);
    hdr[h++] = uint8_t(n);
  }
  return sendAll(fd, hdr, h) && (n == 0 || sendAll(fd, d, n));
}

// Verbindung schließen; Aufrufer hält g_connMu.
void closeConn(Conn& c) {
  if (c.fd < 0) return;
  if (c.ws) push(EventType::Close, c.id);
  ::close(c.fd);
  c.fd = -1;
  c.ws = false;
  c.len = 0;
}

bool headerHas(const char* req, const char* name, char* value, size_t vlen) {
  const size_t nl = std::strlen(name);
  for (const char* p = std::strstr(req, "\r\n"); p && p[2]; p = std::strstr(p + 2, "\r\n")) {
    const char* line = p + 2;
    if (strncasecmp(line, name, nl) != 0 || line[nl] != ':') continue;
    const char* v = line + nl + 1;
    while (*v == ' ') ++v;
    size_t k = 0;
    while (v[k] && v[k] != '\r' && k + 1 < vlen) { value[k] = v[k]; ++k; }
    value[k] = 0;
    return true;
  }
  return false;
}

// HTTP-Anfrage vollständig im Puffer: beantworten. false = Verbindung schließen.
bool handleHttp(Conn& c) {
  char* req = reinterpret_cast<char*>(c.buf);
  char* end = std::strstr(req, "\r\n\r\n");
  if (!end) return c.len < sizeof c.buf - 1;  // noch unvollständig
  end[2] = 0;
  char path[128] = {};
  if (std::sscanf(req, "GET %127s", path) != 1) return false;
  if (char* q = std::strchr(path, '?')) *q = 0;

  char key[64], upg[32];
  if (!std::strcmp(path, "/ws") && headerHas(req, "Sec-WebSocket-Key", key, sizeof key) &&
      headerHas(req, "Upgrade", upg, sizeof upg) && !strcasecmp(upg, "websocket")) {
    Sha1 sha;
    sha.add(key, std::strlen(key));
    static const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    sha.add(kGuid, sizeof kGuid - 1);
    uint8_t dig[20];
    sha.finish(dig);
    char acc[32];
    base64(dig, sizeof dig, acc);
    char resp[256];
    const int n = std::snprintf(resp, sizeof resp,
                                "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    if (!sendAll(c.fd, resp, size_t(n))) return false;
    c.ws = true;
    c.len = 0;
    push(EventType::Open, c.id);
    return true;
  }

  char resp[512];
  for (int i = 0; i < g_pageCount; ++i) {
    if (std::strcmp(path, g_pages[i].path) != 0) continue;
    const int n = std::snprintf(resp, sizeof resp,
                                "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Encoding: gzip\r\n"
                                "Content-Length: %zu\r\nCache-Control: no-store\r\n"
                                "Connection: close\r\n\r\n", g_pages[i].mime, g_pages[i].len);
    sendAll(c.fd, resp, size_t(n));
    sendAll(c.fd, g_pages[i].gz, g_pages[i].len);
    return false;
  }
  int n;
  if (!std::strcmp(path, "/favicon.ico"))
    n = std::snprintf(resp, sizeof resp,
                      "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  else  // wie das Captive Portal am ESP: alles andere auf die Seite umleiten
    n = std::snprintf(resp, sizeof resp,
                      "HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n"
                      "Connection: close\r\n\r\n");
  sendAll(c.fd, resp, size_t(n));
  return false;
}

// WebSocket-Frames im Puffer verarbeiten. false = Verbindung schließen.
bool handleWs(Conn& c) {
  size_t off = 0;
  bool keep = true;
  while (keep && c.len - off >= 2) {
    const uint8_t* p = c.buf + off;
    const uint8_t op = p[0] & 0x0F;
    const bool masked = p[1] & 0x80;
    uint64_t plen = p[1] & 0x7F;
    size_t h = 2;
    if (plen == 126) {
      if (c.len - off < 4) break;
      plen = uint64_t(p[2]) << 8 | p[3];
      h = 4;
    } else if (plen == 127) {
      return false;  // so große Nachrichten gibt es im Protokoll nicht
    }
    if (!masked || plen > sizeof c.buf - 16) return false;  // Client-Frames sind maskiert
    if (c.len - off < h + 4 + plen) break;
    const uint8_t* mask = p + h;
    uint8_t* d = const_cast<uint8_t*>(p + h + 4);
    for (uint64_t i = 0; i < plen; ++i) d[i] ^= mask[i & 3];
    switch (op) {
      case 0x2:
        if (plen <= kMaxMessage) push(EventType::Message, c.id, d, size_t(plen));
        break;
      case 0x8:
        sendFrame(c.fd, 0x8, nullptr, 0);
        keep = false;
        break;
      case 0x9:
        sendFrame(c.fd, 0xA, d, size_t(plen));
        break;
      default:  // Text, Pong, Fortsetzungen: ignorieren
        break;
    }
    off += h + 4 + size_t(plen);
  }
  std::memmove(c.buf, c.buf + off, c.len - off);
  c.len -= off;
  return keep;
}

void serverLoop() {
  pollfd fds[1 + kMaxConn];
  int map[1 + kMaxConn];
  while (!g_stop) {
    int n = 0;
    fds[n] = {g_listen, POLLIN, 0};
    map[n++] = -1;
    {
      std::lock_guard<std::mutex> lk(g_connMu);
      for (int i = 0; i < kMaxConn; ++i)
        if (g_conns[i].fd >= 0) {
          fds[n] = {g_conns[i].fd, POLLIN, 0};
          map[n++] = i;
        }
    }
    if (::poll(fds, nfds_t(n), 100) <= 0) continue;
    std::lock_guard<std::mutex> lk(g_connMu);
    for (int k = 0; k < n; ++k) {
      if (!fds[k].revents) continue;
      if (map[k] < 0) {
        const int fd = ::accept(g_listen, nullptr, nullptr);
        if (fd < 0) continue;
        Conn* free = nullptr;
        for (Conn& c : g_conns)
          if (c.fd < 0) { free = &c; break; }
        if (!free) { ::close(fd); continue; }
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        free->fd = fd;
        free->ws = false;
        free->len = 0;
        free->id = g_nextId++;
        continue;
      }
      Conn& c = g_conns[map[k]];
      if (c.fd != fds[k].fd) continue;
      const ssize_t r = ::recv(c.fd, c.buf + c.len, sizeof c.buf - 1 - c.len, 0);
      if (r <= 0) { closeConn(c); continue; }
      c.len += size_t(r);
      c.buf[c.len] = 0;
      if (!(c.ws ? handleWs(c) : handleHttp(c))) closeConn(c);
    }
  }
}

// Erste IPv4-Adresse einer echten Netzwerkkarte (ohne Loopback, Docker, VPN).
void findHost() {
  std::snprintf(g_host, sizeof g_host, "127.0.0.1");
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return;
  int best = -1;
  for (ifaddrs* a = list; a; a = a->ifa_next) {
    if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
    if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
    const char* nm = a->ifa_name;
    int score = 1;
    if (!std::strncmp(nm, "docker", 6) || !std::strncmp(nm, "br-", 3) ||
        !std::strncmp(nm, "veth", 4) || !std::strncmp(nm, "tun", 3) || !std::strncmp(nm, "virbr", 5))
      score = 0;
    if (nm[0] == 'w' || nm[0] == 'e') score = 2;
    if (score <= best) continue;
    best = score;
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, g_host, sizeof g_host);
  }
  freeifaddrs(list);
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

bool start(const char*, const char*) {
  if (g_running) return true;
  const pd::pc::SimConfig& cfg = pd::pc::config();
  if ((cfg.headless && !cfg.webpadForce) || cfg.webpadPort == 0) return false;
  g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
  if (g_listen < 0) return false;
  const int one = 1;
  setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(uint16_t(cfg.webpadPort));
  if (::bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
      ::listen(g_listen, 8) != 0) {
    std::fprintf(stderr, "[WARN] WebPad: Port %d nicht verfügbar (%s)\n", cfg.webpadPort,
                 std::strerror(errno));
    ::close(g_listen);
    g_listen = -1;
    return false;
  }
  g_port = uint16_t(cfg.webpadPort);
  findHost();
  {
    std::lock_guard<std::mutex> lk(g_queueMu);
    g_qHead = g_qCount = 0;
    g_qWarned = false;
  }
  g_stop = false;
  g_thread = std::thread(serverLoop);
  g_running = true;
  return true;
}

void stop() {
  if (!g_running) return;
  g_stop = true;
  g_thread.join();
  {
    std::lock_guard<std::mutex> lk(g_connMu);
    for (Conn& c : g_conns)
      if (c.fd >= 0) { ::close(c.fd); c.fd = -1; c.ws = false; c.len = 0; }
  }
  ::close(g_listen);
  g_listen = -1;
  g_running = false;
  std::lock_guard<std::mutex> lk(g_queueMu);
  g_qHead = g_qCount = 0;
}

Info info() {
  Info i{};
  i.running = g_running;
  i.accessPoint = false;
  i.port = g_port;
  std::snprintf(i.host, sizeof i.host, "%s", g_host);
  return i;
}

size_t poll(Event* out, size_t max) {
  std::lock_guard<std::mutex> lk(g_queueMu);
  size_t n = 0;
  while (n < max && g_qCount) {
    out[n++] = g_queue[g_qHead];
    g_qHead = (g_qHead + 1) % kQueue;
    --g_qCount;
  }
  return n;
}

bool send(ClientId id, const uint8_t* data, size_t len) {
  std::lock_guard<std::mutex> lk(g_connMu);
  for (Conn& c : g_conns)
    if (c.fd >= 0 && c.ws && c.id == id) {
      if (!sendFrame(c.fd, 0x2, data, len)) closeConn(c);
      return true;
    }
  return false;
}

}  // namespace pd::hal::net

#endif
