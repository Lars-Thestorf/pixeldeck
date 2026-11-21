// Platzhalter für APIs, deren Umsetzung in späteren Meilensteinen folgt.
#include "pd/audio.h"
#include "pd/scores.h"
#include "pd/storage.h"

namespace pd {

void Audio::play(const Sfx&, int) {}     // M5
void Audio::stop(int) {}                 // M5

bool Storage::load(void*, size_t) { return false; }        // M2
bool Storage::save(const void*, size_t) { return false; }  // M2
void Storage::erase() {}                                   // M2

void Scores::submit(int, int32_t) {}     // M4

}  // namespace pd
