#pragma once
#include <cstddef>
#include <cstdint>

#include "pd/config.h"

namespace pd::os {

// Profile (SPEC 7.1), gespeichert in "os/profiles". Dazu pro Gerät das zuletzt genutzte
// Profil, damit Join und Neustart es vorwählen.
constexpr uint8_t kGuest = 0xFF;          // Profil-ID des Gasts (wie im WebPad-Protokoll)
constexpr int kNameLen = 8;
constexpr int kProfileColors = 8;
extern const uint16_t kProfileColor[kProfileColors];
constexpr uint16_t kGuestColor = 0xFFFF;

struct Profile {
  uint8_t id;                             // 1..254, 0 = Eintrag leer
  uint8_t color;                          // Index in kProfileColor
  char    name[kNameLen + 1];
};

class Profiles {
public:
  static constexpr int MAX = PD_MAX_PROFILES;
  static constexpr int MAX_DEVICES = 8;   // gemerkte Geräte -> Profil

  void clear();
  bool load();
  bool save() const;

  int count() const { return count_; }
  const Profile& at(int i) const { return list_[i]; }   // 0..count-1, nach Anlage sortiert
  const Profile* find(uint8_t id) const;
  int indexOf(uint8_t id) const;          // -1, wenn nicht vorhanden

  // Name/Farbe für eine Profil-ID; unbekannte ID und Gast -> "GAST" / Weiß.
  // Für Spieler in Slots InputSystem::slotColor() nehmen (Gäste haben dort eine Farbe).
  const char* name(uint8_t id) const;
  uint16_t color(uint8_t id) const;

  // Neues Profil. Rückgabe: ID oder 0, wenn voll oder Name leer.
  uint8_t create(const char* name, uint8_t color);
  bool update(uint8_t id, const char* name, uint8_t color);
  bool remove(uint8_t id);

  // Zählt jede Änderung der Liste (Anlegen, Ändern, Löschen, Laden); fürs WebPad.
  uint32_t revision() const { return rev_; }

  uint8_t lastFor(uint32_t deviceId) const;     // kGuest, wenn unbekannt
  void remember(uint32_t deviceId, uint8_t profileId);

private:
  struct DevPref { uint32_t deviceId; uint8_t profileId; uint8_t used; uint16_t age; };
  struct Blob {
    uint8_t version;
    uint8_t nextId;
    uint8_t count;
    uint8_t pad;
    Profile list[MAX];
    DevPref dev[MAX_DEVICES];
  };
  static void copyName(char* dst, const char* src);

  Profile list_[MAX] = {};
  int     count_ = 0;
  uint8_t nextId_ = 1;
  DevPref dev_[MAX_DEVICES] = {};
  uint16_t ageCounter_ = 0;
  uint32_t rev_ = 0;
};

}  // namespace pd::os
