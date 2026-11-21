#pragma once
#include <cstddef>

namespace pd {

// SPEC 8.1. Umsetzung folgt in M2, bis dahin: load() liefert false, save() false.
class Storage {
public:
  bool load(void* buf, size_t len);
  bool save(const void* buf, size_t len);   // max. PD_SAVE_MAX
  void erase();
};

}  // namespace pd
