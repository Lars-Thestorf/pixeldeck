#pragma once
#include <cstddef>
#include <cstdint>

#include "audio.h"
#include "gfx.h"
#include "input.h"
#include "lang.h"
#include "scores.h"
#include "storage.h"

namespace pd {

namespace os { class Runtime; class Arena; }

#if defined(__MINGW32__)
// MinGW: "printf" hieße MS-Format ohne %zu; libstdc++ nutzt aber die ANSI-stdio
#define PD_PRINTF_FMT(a, b) __attribute__((format(gnu_printf, a, b)))
#elif defined(__GNUC__)
#define PD_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define PD_PRINTF_FMT(a, b)
#endif

class Context {
public:
  Gfx&     gfx;
  Input&   input;
  Audio&   audio;
  Storage& storage;
  Scores&  scores;

  uint32_t tick() const { return tick_; }   // Ticks seit Spielstart, ohne Pausen
  uint32_t random();                        // OS-PRNG (PCG32)
  uint32_t random(uint32_t maxExclusive);   // 0..maxExclusive-1, gleichverteilt; 0 -> 0

  // Bump-Allocator auf der Arena, nur in init(). Sonst oder bei Speichermangel nullptr.
  void*    alloc(size_t size, size_t align = alignof(std::max_align_t));

  void     log(const char* fmt, ...) PD_PRINTF_FMT(2, 3);

  // Eingestellte Sprache (SPEC 12.4). tr() wählt den passenden von zwei Texten.
  Lang        lang() const;
  const char* tr(const char* en, const char* de) const { return lang() == Lang::De ? de : en; }

  Context(Gfx& g, Input& i, Audio& a, Storage& s, Scores& sc)
      : gfx(g), input(i), audio(a), storage(s), scores(sc) {}
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

private:
  friend class os::Runtime;
  uint32_t tick_ = 0;
  uint64_t rngState_ = 0, rngInc_ = 1;
  os::Arena* arena_ = nullptr;
  bool allocAllowed_ = false;
  const char* gameId_ = "";
};

}  // namespace pd
