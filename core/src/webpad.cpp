#include "webpad.h"

#include <cstdio>
#include <cstring>

#include "lang_impl.h"
#include "pd/hal/random.h"
#include "pd/os/runtime.h"
#include "profiles.h"
#include "runtime_impl.h"
#include "webpad_page.h"

namespace pd::os {

namespace {
WebPad* g_instance = nullptr;  // für webpadRumble
}

bool webpadRumble(uint32_t deviceId, uint16_t ms) {
  if (!WebPad::isWebPadId(deviceId)) return false;
  if (g_instance) g_instance->rumble(deviceId, ms);
  return true;
}

uint32_t WebPad::idFromToken(const uint8_t* t) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (int i = 0; i < 16; ++i) h = (h ^ t[i]) * 16777619u;
  return (h & 0x3FFFFFFFu) | 0x40000000u;  // Gamepads am PC haben das oberste Bit gesetzt
}

void WebPad::reset() {
  for (Client& c : clients_) c = Client{};
  tick_ = 0;
}

void WebPad::ensureCredentials() {
  Settings& s = rt_.settings();
  if (s.apSuffix[0] && s.apPass[0]) return;
  // einmal pro Gerät zufällig erzeugen und merken [Annahme, SPEC 13.1]
  if (!s.apSuffix[0])
    std::snprintf(s.apSuffix, sizeof s.apSuffix, "%04X", unsigned(hal::random::seed() & 0xFFFF));
  if (!s.apPass[0])
    std::snprintf(s.apPass, sizeof s.apPass, "%08u", unsigned(hal::random::seed() % 100000000u));
  saveSettings(s);
}

bool WebPad::start() {
  g_instance = this;
  if (running_) return true;
  ensureCredentials();
  const Settings& s = rt_.settings();
  char ssid[24];
  std::snprintf(ssid, sizeof ssid, "PIXELDECK-%s", s.apSuffix);
  hal::net::serveStatic("/", kWebPadPage, kWebPadPageLen, "text/html; charset=utf-8");
  reset();
  running_ = hal::net::start(ssid, s.apPass);
  if (running_) {
    const hal::net::Info i = hal::net::info();
    if (i.accessPoint)
      logf(LogLevel::Info, "WebPad an: WLAN %s, http://%s/", ssid, i.host);
    else
      logf(LogLevel::Info, "WebPad an: http://%s:%u/", i.host, unsigned(i.port));
  } else {
    logf(LogLevel::Warn, "WebPad: Netz konnte nicht gestartet werden");
  }
  return running_;
}

void WebPad::stop() {
  if (!running_) return;
  hal::net::stop();
  running_ = false;
  reset();
  logf(LogLevel::Info, "WebPad aus");
}

WebPad::Client* WebPad::find(hal::net::ClientId cid) {
  for (Client& c : clients_)
    if (c.used && c.cid == cid) return &c;
  return nullptr;
}

WebPad::Client* WebPad::findDevice(uint32_t id) {
  for (Client& c : clients_)
    if (c.used && c.hello && !c.replaced && c.deviceId == id) return &c;
  return nullptr;
}

const WebPad::Client* WebPad::findDevice(uint32_t id) const {
  return const_cast<WebPad*>(this)->findDevice(id);
}

WebPad::Client* WebPad::alloc(hal::net::ClientId cid) {
  Client* slot = nullptr;
  for (Client& c : clients_)
    if (!c.used) { slot = &c; break; }
  if (!slot) {
    // voll: die am längsten stille Verbindung verdrängen (verlorenes Close-Ereignis)
    slot = &clients_[0];
    for (Client& c : clients_)
      if (tick_ - c.lastSeen > tick_ - slot->lastSeen) slot = &c;
    if (tick_ - slot->lastSeen <= TIMEOUT_TICKS) {
      const uint8_t m[2] = {REJECT, REJECT_FULL};
      hal::net::send(cid, m, sizeof m);
      return nullptr;
    }
  }
  *slot = Client{};
  slot->used = true;
  slot->cid = cid;
  slot->lastSeen = tick_;
  return slot;
}

bool WebPad::live(const Client& c) const {
  return c.used && c.hello && !c.replaced && tick_ - c.lastSeen <= TIMEOUT_TICKS;
}

int WebPad::clientCount() const {
  int n = 0;
  for (const Client& c : clients_)
    if (live(c)) ++n;
  return n;
}

void WebPad::send(const Client& c, const uint8_t* d, size_t n) { hal::net::send(c.cid, d, n); }

void WebPad::update(uint32_t tick) {
  tick_ = tick;
  if (!running_) return;
  hal::net::Event ev[16];
  for (int round = 0; round < 8; ++round) {  // höchstens 128 Ereignisse pro Tick
    const size_t n = hal::net::poll(ev, 16);
    for (size_t i = 0; i < n; ++i) {
      const hal::net::Event& e = ev[i];
      Client* c = find(e.client);
      switch (e.type) {
        case hal::net::EventType::Open:
          if (c) *c = Client{};
          alloc(e.client);
          break;
        case hal::net::EventType::Close:
          if (c) {
            if (c->hello && !c->replaced)
              logf(LogLevel::Info, "WebPad %08x getrennt (Verbindung %u)", unsigned(c->deviceId),
                   unsigned(c->cid));
            *c = Client{};
          }
          break;
        case hal::net::EventType::Message:
          if (!c) {
            // Open verpasst (z. B. Warteschlange voll): neu anlegen und HELLO anfordern
            c = alloc(e.client);
            if (!c) break;
          }
          handle(*c, e.data, e.len);
          break;
      }
    }
    if (n < 16) break;
  }
}

void WebPad::handle(Client& c, const uint8_t* d, size_t n) {
  if (n == 0 || c.replaced) return;
  const uint8_t type = d[0];
  if (type != HELLO && !c.hello) {
    logf(LogLevel::Info, "WebPad: Verbindung %u ohne HELLO, fordere es an", unsigned(c.cid));
    const uint8_t m[1] = {RESYNC};
    send(c, m, 1);
    c.lastSeen = tick_;
    return;
  }
  // Diagnose: Lücken im Nachrichtenstrom (Heartbeat alle 100 ms, Ping alle 500 ms)
  if (c.hello && tick_ - c.lastSeen > 15)
    logf(LogLevel::Info, "WebPad %08x: Lücke %u ms (Verbindung %u)", unsigned(c.deviceId),
         unsigned((tick_ - c.lastSeen) * 1000u / PD_TICK_HZ), unsigned(c.cid));
  c.timedOut = false;
  Profiles& pr = rt_.profiles();
  switch (type) {
    case HELLO: {
      if (n < 19) return;
      if (d[1] != kVersion) {
        const uint8_t m[2] = {REJECT, REJECT_VERSION};
        send(c, m, 2);
        return;
      }
      const uint32_t id = idFromToken(d + 2);
      // dasselbe Handy in einer zweiten Verbindung (neuer Tab, Neuverbindung): die neue gewinnt
      for (Client& o : clients_) {
        if (&o == &c || !o.used || !o.hello || o.replaced || o.deviceId != id) continue;
        const uint8_t m[2] = {REJECT, REJECT_REPLACED};
        send(o, m, 2);
        o.replaced = true;
        logf(LogLevel::Info, "WebPad %08x: Verbindung %u ersetzt %u (zuletzt vor %u ms)",
             unsigned(id), unsigned(c.cid), unsigned(o.cid),
             unsigned((tick_ - o.lastSeen) * 1000u / PD_TICK_HZ));
      }
      const bool first = !c.hello;
      c.hello = true;
      c.deviceId = id;
      c.profile = pr.find(d[18]) ? d[18] : kGuest;
      c.state = hal::input::RawInput{0, 0, 0, 0, 0};
      c.latch = 0;
      c.lastSeen = tick_;
      c.sentSlot = c.sentProfile = c.sentColor = 0xFE;
      c.revSent = false;
      sendWelcome(c);
      if (first)
        logf(LogLevel::Info, "WebPad %08x verbunden (%s, Verbindung %u)", unsigned(id),
             pr.name(c.profile), unsigned(c.cid));
      applyProfile(c);  // Neuverbindung in reservierten Slot: Profil bleibt, sonst übernehmen
      break;
    }
    case STATE: {
      if (n < 7) return;
      c.state.buttons = uint16_t(d[1] | (d[2] << 8));
      c.state.lx = int8_t(d[3]);
      c.state.ly = int8_t(d[4]);
      c.state.rx = int8_t(d[5]);
      c.state.ry = int8_t(d[6]);
      c.latch |= c.state.buttons;
      c.lastSeen = tick_;
      break;
    }
    case PING: {  // Antwortzeit und Verbindungsanzeige auf dem Handy
      if (n < 3) return;
      c.lastSeen = tick_;
      const uint8_t m[3] = {PONG, d[1], d[2]};
      send(c, m, 3);
      break;
    }
    case PROFILE_CREATE: {
      if (n < 10) return;
      c.lastSeen = tick_;
      char name[kNameLen + 1];
      std::memcpy(name, d + 1, kNameLen);
      name[kNameLen] = 0;
      const uint8_t id = pr.create(name, d[9]);
      if (id == 0) {
        const uint8_t m[2] = {REJECT, REJECT_PROFILE};
        send(c, m, 2);
        break;
      }
      rt_.saveProfiles();
      logf(LogLevel::Info, "WebPad: Profil %s angelegt", pr.name(id));
      c.profile = id;
      const uint8_t m[2] = {PROFILE_SET, id};
      send(c, m, 2);
      applyProfile(c);
      break;
    }
    case PROFILE_SELECT: {
      if (n < 2) return;
      c.lastSeen = tick_;
      c.profile = pr.find(d[1]) ? d[1] : kGuest;
      applyProfile(c);
      break;
    }
    default:
      break;
  }
}

void WebPad::applyProfile(Client& c) {
  InputSystem& is = rt_.inputSys();
  const int s = is.slotOfDevice(c.deviceId);
  if (s < 0) return;
  if (is.slot(s).profileId == c.profile) return;
  if (is.profileInUse(c.profile, s)) return;  // vergeben: bleibt beim bisherigen Profil
  is.setSlotProfile(s, c.profile);
}

uint8_t WebPad::requestedProfile(uint32_t deviceId) const {
  const Client* c = findDevice(deviceId);
  return c && rt_.profiles().find(c->profile) ? c->profile : kGuest;
}

void WebPad::profileSetByConsole(uint32_t deviceId, uint8_t profileId) {
  Client* c = findDevice(deviceId);
  if (!c) return;
  c->profile = profileId;
  const uint8_t m[2] = {PROFILE_SET, profileId};
  send(*c, m, 2);
}

size_t WebPad::devices(hal::input::Device* out, size_t max) {
  size_t n = 0;
  int k = 0;
  for (Client& c : clients_) {
    if (!c.used || !c.hello || c.replaced) continue;
    ++k;
    if (!live(c)) {
      if (!c.timedOut)
        logf(LogLevel::Info, "WebPad %08x: Timeout, %u ms ohne Nachricht (Verbindung %u)",
             unsigned(c.deviceId), unsigned((tick_ - c.lastSeen) * 1000u / PD_TICK_HZ),
             unsigned(c.cid));
      c.timedOut = true;
      continue;
    }
    if (n >= max) continue;
    hal::input::Device& d = out[n++];
    d.id = c.deviceId;
    d.type = hal::input::DeviceType::WebPad;
    std::snprintf(d.name, sizeof d.name, "WEBPAD %d", k & 15);
    d.connected = true;
    if (tick_ - c.lastSeen > STALE_TICKS) {  // Funkloch: nichts festhalten
      d.state = hal::input::RawInput{0, 0, 0, 0, 0};
      d.state.buttons = c.latch;
    } else {
      d.state = c.state;
      d.state.buttons = uint16_t(c.state.buttons | c.latch);
    }
    c.latch = 0;
  }
  return n;
}

void WebPad::sendProfiles(Client& c) {
  const Profiles& pr = rt_.profiles();
  uint8_t m[2 + Profiles::MAX * (2 + kNameLen)];
  size_t k = 0;
  m[k++] = PROFILES;
  m[k++] = uint8_t(pr.count());
  for (int i = 0; i < pr.count(); ++i) {
    const Profile& p = pr.at(i);
    m[k++] = p.id;
    m[k++] = p.color;
    std::memset(m + k, 0, kNameLen);
    std::memcpy(m + k, p.name, std::strlen(p.name));
    k += kNameLen;
  }
  send(c, m, k);
  c.sentRev = pr.revision();
  c.revSent = true;
}

// WELCOME: Version, Palette und (seit SPEC 0.10) die Sprache als letztes Byte
void WebPad::sendWelcome(const Client& c) {
  uint8_t m[4 + 2 * kProfileColors];
  m[0] = WELCOME;
  m[1] = kVersion;
  m[2] = kProfileColors;
  for (int i = 0; i < kProfileColors; ++i) {
    m[3 + 2 * i] = uint8_t(kProfileColor[i] & 0xFF);
    m[4 + 2 * i] = uint8_t(kProfileColor[i] >> 8);
  }
  m[3 + 2 * kProfileColors] = uint8_t(g_lang);
  send(c, m, sizeof m);
}

void WebPad::langChanged() {
  if (!running_) return;
  for (const Client& c : clients_)
    if (c.used && c.hello && !c.replaced) sendWelcome(c);
}

void WebPad::sync() {
  if (!running_) return;
  const InputSystem& is = rt_.inputSys();
  const Profiles& pr = rt_.profiles();
  for (Client& c : clients_) {
    if (!c.used || !c.hello || c.replaced) continue;
    if (!c.revSent || c.sentRev != pr.revision()) {
      sendProfiles(c);
      if (c.profile != kGuest && !pr.find(c.profile)) c.profile = kGuest;  // gelöscht
    }
    const int s = is.slotOfDevice(c.deviceId);
    const uint8_t slot = s < 0 ? 0xFF : uint8_t(s);
    const uint8_t prof = s < 0 ? c.profile : is.slot(s).profileId;
    const uint8_t col = s < 0 ? 0xFF : is.colorIndex(s);  // Gastfarbe kann sich ändern
    if (slot != c.sentSlot || prof != c.sentProfile || col != c.sentColor) {
      const uint8_t m[4] = {SLOT, slot, prof, col};
      send(c, m, 4);
      c.sentSlot = slot;
      c.sentProfile = prof;
      c.sentColor = col;
    }
  }
}

bool WebPad::rumble(uint32_t deviceId, uint16_t ms) {
  Client* c = findDevice(deviceId);
  if (!c || !running_) return false;
  const uint8_t m[3] = {RUMBLE, uint8_t(ms & 0xFF), uint8_t(ms >> 8)};
  send(*c, m, 3);
  return true;
}

}  // namespace pd::os
