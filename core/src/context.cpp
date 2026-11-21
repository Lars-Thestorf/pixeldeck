#include "pd/context.h"

#include <cstdarg>
#include <cstdio>

#include "arena.h"
#include "pcg32.h"
#include "pd/os/runtime.h"

namespace pd {

uint32_t Context::random() { return os::Pcg32::next(rngState_, rngInc_); }

uint32_t Context::random(uint32_t maxExclusive) {
  if (maxExclusive <= 1) return 0;
  // Rejection Sampling gegen Modulo-Verzerrung
  uint32_t threshold = uint32_t(-maxExclusive) % maxExclusive;
  for (;;) {
    uint32_t r = random();
    if (r >= threshold) return r % maxExclusive;
  }
}

void* Context::alloc(size_t size, size_t align) {
  if (!allocAllowed_ || !arena_) {
    os::logf(os::LogLevel::Error, "[%s] ctx.alloc(%zu) außerhalb von init() - abgelehnt",
             gameId_, size);
    return nullptr;
  }
  void* p = arena_->alloc(size, align);
  if (!p)
    os::logf(os::LogLevel::Error, "[%s] ctx.alloc(%zu): Arena voll (%zu/%zu Bytes belegt)",
             gameId_, size, arena_->used(), arena_->capacity());
  return p;
}

void Context::log(const char* fmt, ...) {
  char msg[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  os::logf(os::LogLevel::Info, "[%s] %s", gameId_, msg);
}

}  // namespace pd
