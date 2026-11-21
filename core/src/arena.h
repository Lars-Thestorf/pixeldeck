#pragma once
#include <cstddef>
#include <cstdint>

namespace pd::os {

// Bump-Allocator über einem festen Speicherblock (SPEC 9). Kein free.
class Arena {
public:
  Arena(uint8_t* mem, size_t capacity) : mem_(mem), cap_(capacity) {}

  void* alloc(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) return nullptr;
    uintptr_t base = reinterpret_cast<uintptr_t>(mem_);
    uintptr_t p = (base + used_ + (align - 1)) & ~uintptr_t(align - 1);
    size_t off = size_t(p - base);
    if (off > cap_ || size > cap_ - off) return nullptr;
    used_ = off + size;
    if (used_ > peak_) peak_ = used_;
    return mem_ + off;
  }

  void   reset() { used_ = 0; }          // Höchststand bleibt
  void   resetPeak() { peak_ = used_; }
  size_t used() const { return used_; }
  size_t peak() const { return peak_; }
  size_t capacity() const { return cap_; }

private:
  uint8_t* mem_;
  size_t cap_;
  size_t used_ = 0, peak_ = 0;
};

}  // namespace pd::os
