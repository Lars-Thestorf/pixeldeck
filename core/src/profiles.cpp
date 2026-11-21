#include "profiles.h"

#include <cstring>

#include "pd/color.h"
#include "pd/hal/storage.h"

namespace pd::os {

// [Annahme] 8 gut unterscheidbare Farben auf der Matrix
const uint16_t kProfileColor[kProfileColors] = {
    rgb(255, 40, 40),   rgb(255, 140, 0),  rgb(255, 230, 0),  rgb(40, 220, 40),
    rgb(0, 220, 220),   rgb(50, 90, 255),  rgb(170, 60, 255), rgb(255, 80, 180),
};

namespace {
constexpr uint8_t kVersion = 1;
}

void Profiles::copyName(char* dst, const char* src) {
  int n = 0;
  for (; src && src[n] && n < kNameLen; ++n) {
    char c = src[n];
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c < 32 || c > 126) c = ' ';
    dst[n] = c;
  }
  while (n > 0 && dst[n - 1] == ' ') --n;  // Leerzeichen am Ende weg
  dst[n] = 0;
}

void Profiles::clear() {
  *this = Profiles{};
}

bool Profiles::load() {
  clear();
  static Blob b;  // ~300 Byte, nicht auf den Stack
  size_t got = 0;
  if (!hal::storage::load("os", "profiles", &b, sizeof b, &got)) return false;
  if (got != sizeof b || b.version != kVersion) return false;
  for (int i = 0; i < MAX && i < b.count; ++i) {
    const Profile& p = b.list[i];
    if (p.id == 0 || p.id == kGuest || find(p.id)) continue;
    Profile& q = list_[count_++];
    q.id = p.id;
    q.color = uint8_t(p.color % kProfileColors);
    char tmp[kNameLen + 1];
    std::memcpy(tmp, p.name, kNameLen);
    tmp[kNameLen] = 0;
    copyName(q.name, tmp);
    if (!q.name[0]) --count_;
  }
  nextId_ = b.nextId == 0 || b.nextId == kGuest ? 1 : b.nextId;
  std::memcpy(dev_, b.dev, sizeof dev_);
  for (DevPref& d : dev_) {
    if (d.used && d.profileId != kGuest && !find(d.profileId)) d.profileId = kGuest;
    if (d.age > ageCounter_) ageCounter_ = d.age;
  }
  return true;
}

bool Profiles::save() const {
  static Blob b;
  std::memset(&b, 0, sizeof b);
  b.version = kVersion;
  b.nextId = nextId_;
  b.count = uint8_t(count_);
  std::memcpy(b.list, list_, sizeof b.list);
  std::memcpy(b.dev, dev_, sizeof b.dev);
  return hal::storage::save("os", "profiles", &b, sizeof b);
}

const Profile* Profiles::find(uint8_t id) const {
  const int i = indexOf(id);
  return i < 0 ? nullptr : &list_[i];
}

int Profiles::indexOf(uint8_t id) const {
  if (id == 0 || id == kGuest) return -1;
  for (int i = 0; i < count_; ++i)
    if (list_[i].id == id) return i;
  return -1;
}

const char* Profiles::name(uint8_t id) const {
  const Profile* p = find(id);
  return p ? p->name : "GAST";
}

uint16_t Profiles::color(uint8_t id) const {
  const Profile* p = find(id);
  return p ? kProfileColor[p->color % kProfileColors] : kGuestColor;
}

uint8_t Profiles::create(const char* name, uint8_t color) {
  if (count_ >= MAX) return 0;
  char n[kNameLen + 1];
  copyName(n, name);
  if (!n[0]) return 0;
  // nächste freie ID, 1..254; IDs gelöschter Profile werden erst nach einem Umlauf wieder
  // vergeben, damit alte Highscore-Einträge nicht sofort einem neuen Profil zugeordnet werden
  uint8_t id = nextId_;
  for (int tries = 0; tries < 254; ++tries) {
    if (id == 0 || id == kGuest) id = 1;
    if (!find(id)) break;
    ++id;
  }
  Profile& p = list_[count_++];
  p.id = id;
  p.color = uint8_t(color % kProfileColors);
  std::memcpy(p.name, n, sizeof p.name);
  nextId_ = uint8_t(id + 1);
  if (nextId_ == kGuest || nextId_ == 0) nextId_ = 1;
  return id;
}

bool Profiles::update(uint8_t id, const char* name, uint8_t color) {
  const int i = indexOf(id);
  if (i < 0) return false;
  char n[kNameLen + 1];
  copyName(n, name);
  if (!n[0]) return false;
  std::memcpy(list_[i].name, n, sizeof n);
  list_[i].color = uint8_t(color % kProfileColors);
  return true;
}

bool Profiles::remove(uint8_t id) {
  const int i = indexOf(id);
  if (i < 0) return false;
  for (int k = i; k + 1 < count_; ++k) list_[k] = list_[k + 1];
  --count_;
  list_[count_] = Profile{};
  for (DevPref& d : dev_)
    if (d.used && d.profileId == id) d.profileId = kGuest;
  return true;
}

uint8_t Profiles::lastFor(uint32_t deviceId) const {
  for (const DevPref& d : dev_)
    if (d.used && d.deviceId == deviceId) return find(d.profileId) ? d.profileId : kGuest;
  return kGuest;
}

void Profiles::remember(uint32_t deviceId, uint8_t profileId) {
  DevPref* slot = nullptr;
  for (DevPref& d : dev_)
    if (d.used && d.deviceId == deviceId) slot = &d;
  if (!slot) {
    // freien oder am längsten ungenutzten Eintrag nehmen
    slot = &dev_[0];
    for (DevPref& d : dev_) {
      if (!d.used) { slot = &d; break; }
      if (d.age < slot->age) slot = &d;
    }
  }
  slot->used = 1;
  slot->deviceId = deviceId;
  slot->profileId = profileId;
  slot->age = ++ageCounter_;
}

}  // namespace pd::os
