// Storage-HAL am PC: eine Datei pro Schlüssel unter <dataDir>/<ns>/<key>.bin (SPEC 8.2).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "pd/hal/storage.h"
#include "sim.h"

namespace fs = std::filesystem;

namespace {

bool validName(const char* s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (; s[n]; ++n) {
    char c = s[n];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return n <= 15;
}

fs::path baseDir() {
  const std::string& d = pd::pc::config().dataDir;
  if (!d.empty()) return fs::path(d);
#ifdef _WIN32
  const char* app = std::getenv("APPDATA");
  return fs::path(app ? app : ".") / "pixeldeck";
#else
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / ".pixeldeck";
#endif
}

fs::path file(const char* ns, const char* key) {
  return baseDir() / ns / (std::string(key) + ".bin");
}

}  // namespace

namespace pd::hal::storage {

bool load(const char* ns, const char* key, void* buf, size_t len, size_t* outLen) {
  if (outLen) *outLen = 0;
  if (!validName(ns) || !validName(key)) return false;
  FILE* f = std::fopen(file(ns, key).string().c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  bool ok = size >= 0;
  if (ok && outLen) *outLen = size_t(size);
  // Bei abweichender Größe nur melden; der Aufrufer verwirft dann.
  if (ok && size_t(size) <= len) ok = std::fread(buf, 1, size_t(size), f) == size_t(size);
  std::fclose(f);
  return ok;
}

bool save(const char* ns, const char* key, const void* buf, size_t len) {
  if (!validName(ns) || !validName(key)) return false;
  std::error_code ec;
  fs::create_directories(baseDir() / ns, ec);
  if (ec) return false;
  const fs::path target = file(ns, key);
  const fs::path tmp = target.string() + ".tmp";
  FILE* f = std::fopen(tmp.string().c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(buf, 1, len, f) == len;
  ok = (std::fclose(f) == 0) && ok;
  if (ok) fs::rename(tmp, target, ec);  // atomar ersetzen
  return ok && !ec;
}

bool erase(const char* ns, const char* key) {
  if (!validName(ns) || !validName(key)) return false;
  std::error_code ec;
  return fs::remove(file(ns, key), ec);
}

}  // namespace pd::hal::storage
