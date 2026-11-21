// Debug-Hook (SPEC 3): meldet Heap-Allokationen über operator new, während
// Spielcode läuft. malloc() direkt wird nicht erfasst.
#include <cstdio>
#include <cstdlib>
#include <new>

#include "pd/os/runtime.h"

namespace {
int g_reports = 0;

void* allocChecked(std::size_t n) {
  if (pd::os::gameCodeActive() && g_reports < 10) {
    ++g_reports;
    std::fprintf(stderr,
                 "[WARN] Heap-Allokation (%zu Bytes) im Spielcode. Spiele dürfen nur "
                 "ctx.alloc() in init() nutzen.%s\n",
                 n, g_reports == 10 ? " (weitere Meldungen unterdrückt)" : "");
  }
  if (void* p = std::malloc(n ? n : 1)) return p;
  std::fprintf(stderr, "[ERROR] operator new: kein Speicher\n");
  std::abort();
}
}  // namespace

void* operator new(std::size_t n) { return allocChecked(n); }
void* operator new[](std::size_t n) { return allocChecked(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocChecked(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocChecked(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
