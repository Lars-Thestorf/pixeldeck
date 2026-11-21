#include "score_table.h"

#include <cstdio>
#include <cstring>

#include "pd/hal/storage.h"

namespace pd::os {

namespace {
void nsFor(char (&ns)[16], const char* gameId) { std::snprintf(ns, sizeof ns, "s_%s", gameId); }
}  // namespace

bool ScoreTable::load(const char* gameId) {
  *this = ScoreTable{};
  char ns[16];
  nsFor(ns, gameId);
  ScoreTable tmp;
  size_t got = 0;
  if (!hal::storage::load(ns, "top", &tmp, sizeof tmp, &got)) return false;
  if (got != sizeof tmp || tmp.version != ScoreTable{}.version || tmp.count > MAX) return false;
  for (int i = 0; i < tmp.count; ++i) tmp.e[i].name[kNameLen] = 0;
  *this = tmp;
  return true;
}

bool ScoreTable::save(const char* gameId) const {
  char ns[16];
  nsFor(ns, gameId);
  return hal::storage::save(ns, "top", this, sizeof *this);
}

bool ScoreTable::reset(const char* gameId) {
  char ns[16];
  nsFor(ns, gameId);
  return hal::storage::erase(ns, "top");
}

bool ScoreTable::counts(ScoreMode mode, int32_t score) {
  if (mode == ScoreMode::None) return false;
  if (mode == ScoreMode::Higher && score <= 0) return false;
  return true;
}

int ScoreTable::insert(ScoreMode mode, int32_t score, uint8_t profileId, const char* name) {
  if (!counts(mode, score)) return -1;
  int pos = count;
  for (int i = 0; i < count; ++i) {
    const bool better = mode == ScoreMode::Higher ? score > e[i].score : score < e[i].score;
    if (better) { pos = i; break; }
  }
  if (pos >= MAX) return -1;
  const int last = count < MAX ? count : MAX - 1;
  for (int i = last; i > pos; --i) e[i] = e[i - 1];
  if (count < MAX) ++count;
  ScoreEntry& s = e[pos];
  s = ScoreEntry{};
  s.score = score;
  s.profileId = profileId;
  std::snprintf(s.name, sizeof s.name, "%s", name ? name : "GAST");
  return pos;
}

}  // namespace pd::os
