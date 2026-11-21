#include "pd/storage.h"

#include <cstring>

#include "pd/config.h"
#include "pd/hal/storage.h"
#include "os_call.h"
#include "pd/os/runtime.h"

namespace pd {

namespace {
constexpr const char* kKey = "save";

// Kopf vor den Spieldaten. Der Puffer liegt statisch im .bss (4 KB + Kopf),
// weil die HAL einen zusammenhängenden Block erwartet.
struct Header {
  uint16_t magic;    // 'P','D'
  uint16_t version;  // saveVersion des Spiels
  uint32_t len;      // Nutzdaten
};
constexpr uint16_t kMagic = 0x4450;
alignas(4) uint8_t g_buf[sizeof(Header) + PD_SAVE_MAX];
}  // namespace

bool Storage::load(void* buf, size_t len) {
  os::OsCallScope osCall;
  if (!ns_[0] || !buf || len > PD_SAVE_MAX) return false;
  size_t got = 0;
  if (!hal::storage::load(ns_, kKey, g_buf, sizeof(Header) + len, &got)) return false;
  Header h;
  std::memcpy(&h, g_buf, sizeof h);
  if (got != sizeof(Header) + len || h.magic != kMagic || h.version != version_ || h.len != len)
    return false;
  std::memcpy(buf, g_buf + sizeof(Header), len);
  return true;
}

bool Storage::save(const void* buf, size_t len) {
  os::OsCallScope osCall;
  if (!ns_[0] || !buf) return false;
  if (len > PD_SAVE_MAX) {
    os::logf(os::LogLevel::Error, "Spielstand %zu Bytes > PD_SAVE_MAX (%d)", len, PD_SAVE_MAX);
    return false;
  }
  const Header h{kMagic, version_, uint32_t(len)};
  std::memcpy(g_buf, &h, sizeof h);
  std::memcpy(g_buf + sizeof h, buf, len);
  return hal::storage::save(ns_, kKey, g_buf, sizeof h + len);
}

void Storage::erase() {
  os::OsCallScope osCall;
  if (ns_[0]) hal::storage::erase(ns_, kKey);
}

}  // namespace pd
