#pragma once
#include <cstdint>

namespace pd {

// SPEC 7.2. Umsetzung folgt in M4, bis dahin No-Op.
class Scores {
public:
  void submit(int slot, int32_t score);
};

}  // namespace pd
