#pragma once

namespace pd::os {

bool exchangeGameCodeActive(bool active);  // runtime.cpp

// Markiert einen Aufruf aus dem Spiel in OS- oder HAL-Dienste (Storage, Rumble).
// Allokationen dort gehören nicht dem Spiel und sollen den Heap-Hook nicht auslösen.
class OsCallScope {
public:
  OsCallScope() : prev_(exchangeGameCodeActive(false)) {}
  ~OsCallScope() { exchangeGameCodeActive(prev_); }
  OsCallScope(const OsCallScope&) = delete;
  OsCallScope& operator=(const OsCallScope&) = delete;

private:
  bool prev_;
};

}  // namespace pd::os
