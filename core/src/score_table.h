#pragma once
#include <cstdint>

#include "pd/registry.h"
#include "profiles.h"

namespace pd::os {

// Top 10 eines Spiels (SPEC 7.2) im Namensraum "s_<id>", Schlüssel "top".
struct ScoreEntry {
  int32_t score;
  uint8_t profileId;                      // kGuest für Gast
  char    name[kNameLen + 1];             // Kopie, überlebt das Löschen des Profils
};

struct ScoreTable {
  static constexpr int MAX = 10;
  uint8_t version = 1;
  uint8_t count = 0;
  ScoreEntry e[MAX] = {};

  bool load(const char* gameId);
  bool save(const char* gameId) const;
  static bool reset(const char* gameId);

  // Fügt ein, falls der Score in die Liste kommt. Bei Gleichstand steht der ältere vorn.
  // Rückgabe: Platz 0..9 oder -1.
  int insert(ScoreMode mode, int32_t score, uint8_t profileId, const char* name);

  // Zählt der Score überhaupt? [Annahme] Bei Higher keine Scores <= 0.
  static bool counts(ScoreMode mode, int32_t score);
};

}  // namespace pd::os
