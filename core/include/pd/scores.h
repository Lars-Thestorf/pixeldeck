#pragma once
#include <cstdint>

namespace pd {

namespace os { class Runtime; }

// Highscores (SPEC 7.2). Der Score geht an das Profil im Slot; Top 10 pro Spiel.
// Ohne scoreMode oder außerhalb eines Spiels: No-Op.
class Scores {
public:
  void submit(int slot, int32_t score);

private:
  friend class os::Runtime;
  os::Runtime* rt_ = nullptr;
};

}  // namespace pd
