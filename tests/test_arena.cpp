#include <doctest.h>

#include <cstdint>

#include "arena.h"

using pd::os::Arena;

TEST_CASE("Arena: Ausrichtung, Höchststand, Überlauf") {
  alignas(16) static uint8_t mem[256];
  Arena a(mem, sizeof mem);
  void* p1 = a.alloc(3, 1);
  REQUIRE(p1 == mem);
  void* p2 = a.alloc(8, 8);
  REQUIRE(p2 != nullptr);
  CHECK(reinterpret_cast<uintptr_t>(p2) % 8 == 0);
  CHECK(a.used() == 16);
  CHECK(a.alloc(1000, 1) == nullptr);
  CHECK(a.used() == 16);
  CHECK(a.alloc(8, 3) == nullptr);  // keine Zweierpotenz
  CHECK(a.alloc(240, 1) != nullptr);
  CHECK(a.alloc(1, 1) == nullptr);
  CHECK(a.peak() == 256);
  a.reset();
  CHECK(a.used() == 0);
  CHECK(a.peak() == 256);
  a.resetPeak();
  CHECK(a.peak() == 0);
}
