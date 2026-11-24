#pragma once
#include <cstddef>
#include <cstdint>

#include "pd/hal/input.h"
#include "pd/hal/net.h"

namespace pd::os {

class Runtime;

// WebPad (SPEC 13): Handy-Controller über WLAN. Protokoll und Geräteverwaltung liegen hier,
// die Plattform liefert nur den Transport (hal::net).
//
// Jede WebSocket-Verbindung ist ein Client. Nach HELLO ist er ein Gerät vom Typ WebPad mit
// stabiler ID aus dem Token des Browsers und läuft durch denselben Slot-Manager wie alle
// anderen Geräte (Beitritt mit A, Reservierung bei Trennung).
class WebPad {
public:
  static constexpr int MAX_CLIENTS = 8;            // Verbindungen, auch ohne HELLO
  static constexpr uint8_t kVersion = 1;           // Protokollversion
  // Funklöcher von 1-2 s kommen am Handy beim Spielen vor (verlorenes Paket, TCP wiederholt mit
  // wachsender Wartezeit). Deshalb zweistufig: nach STALE_TICKS gelten die Tasten als
  // losgelassen (nichts bleibt hängen), getrennt wird erst nach TIMEOUT_TICKS.
  static constexpr uint32_t STALE_TICKS = 60;      // 1 s ohne Nachricht = Tasten los
  static constexpr uint32_t TIMEOUT_TICKS = 150;   // 2,5 s ohne Nachricht = getrennt

  // Nachrichtentypen (SPEC 13.2)
  enum : uint8_t {
    HELLO = 0x01, STATE = 0x02, PROFILE_CREATE = 0x03, PROFILE_SELECT = 0x04, PING = 0x05,
    WELCOME = 0x81, SLOT = 0x82, RUMBLE = 0x83, PROFILES = 0x84, PROFILE_SET = 0x85,
    RESYNC = 0x86, REJECT = 0x87, PONG = 0x88,
  };
  enum : uint8_t { REJECT_FULL = 1, REJECT_VERSION = 2, REJECT_REPLACED = 3, REJECT_PROFILE = 4 };

  explicit WebPad(Runtime& rt) : rt_(rt) {}

  // Netz starten/stoppen. start() registriert die Seite und baut SSID und Passwort aus den
  // Einstellungen (legt sie beim ersten Mal an).
  bool start();
  void stop();
  bool running() const { return running_; }
  // SSID-Suffix und Passwort in den Einstellungen anlegen, falls leer (zufällig, gespeichert).
  void ensureCredentials();

  // Einmal pro Tick vor dem Input-Update: Ereignisse abholen und verarbeiten.
  void update(uint32_t tick);
  // Verbundene WebPads als Geräte für den Slot-Manager. Kurze Drücke zwischen zwei Ticks
  // werden für einen Tick gehalten, damit sie nicht verloren gehen.
  size_t devices(hal::input::Device* out, size_t max);
  // Nach dem Input-Update: Slot, Profil und Profilliste an die Handys melden, wenn geändert.
  void sync();

  // Profil eines Slots wurde an der Konsole geändert (Systemmenü): Handy übernimmt es.
  void profileSetByConsole(uint32_t deviceId, uint8_t profileId);
  // Profil, mit dem das Handy spielen will (beim Beitritt), kGuest wenn unbekannt.
  uint8_t requestedProfile(uint32_t deviceId) const;
  bool rumble(uint32_t deviceId, uint16_t ms);
  int  clientCount() const;  // Handys nach HELLO

  static bool isWebPadId(uint32_t id) { return (id & 0xC0000000u) == 0x40000000u; }
  static uint32_t idFromToken(const uint8_t* token16);

  void reset();  // alles vergessen, ohne Netz anzufassen

private:
  struct Client {
    bool     used = false;
    bool     hello = false;
    bool     replaced = false;      // gleiches Token in neuerer Verbindung, wird ignoriert
    hal::net::ClientId cid = 0;
    uint32_t deviceId = 0;
    uint8_t  profile = 0xFF;        // gewünschtes Profil
    hal::input::RawInput state{};
    uint16_t latch = 0;             // seit dem letzten Tick gedrückte Buttons
    uint32_t lastSeen = 0;
    uint8_t  sentSlot = 0xFE, sentProfile = 0xFE;
    uint32_t sentRev = 0;
    bool     revSent = false;
    bool     timedOut = false;      // Timeout schon gemeldet (Diagnose)
  };

  Client* find(hal::net::ClientId cid);
  Client* findDevice(uint32_t deviceId);
  const Client* findDevice(uint32_t deviceId) const;
  Client* alloc(hal::net::ClientId cid);
  void handle(Client& c, const uint8_t* d, size_t n);
  void sendProfiles(Client& c);
  void send(const Client& c, const uint8_t* d, size_t n);
  void applyProfile(Client& c);   // gewünschtes Profil in den Slot übernehmen, wenn frei
  bool live(const Client& c) const;

  Runtime& rt_;
  bool running_ = false;
  uint32_t tick_ = 0;
  Client clients_[MAX_CLIENTS];
};

// Für Player::rumble (input_system.cpp): true, wenn die ID einem WebPad gehört.
bool webpadRumble(uint32_t deviceId, uint16_t ms);

}  // namespace pd::os
