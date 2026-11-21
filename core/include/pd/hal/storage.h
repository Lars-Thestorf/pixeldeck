#pragma once
#include <cstddef>

// Namensraum und Schlüssel: nur [a-z0-9_], max. 15 Zeichen (NVS-Grenze).
namespace pd::hal::storage {
bool load(const char* ns, const char* key, void* buf, size_t len, size_t* outLen);
bool save(const char* ns, const char* key, const void* buf, size_t len);
bool erase(const char* ns, const char* key);
}  // namespace pd::hal::storage
