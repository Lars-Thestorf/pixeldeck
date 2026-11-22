#pragma once
#include <cstddef>
#include <cstdint>

// Transport für das WebPad (SPEC 11, 13). Das Protokoll liegt im Core (core/src/webpad.*).
//
// Abweichung vom Entwurf in SPEC 11: statt Callbacks aus dem Netz-Task sammelt die Plattform
// Ereignisse in einer Warteschlange, die der Core einmal pro Tick mit poll() abholt. Damit
// bleibt der Core single-threaded; die Sperre liegt in der Plattform.
namespace pd::hal::net {

using ClientId = uint32_t;

// Seite registrieren (vor start()). gz = gzip-komprimierter Inhalt, liegt dauerhaft im Flash.
void serveStatic(const char* path, const uint8_t* gz, size_t len, const char* mime);

// ESP32: Access Point (WPA2, leeres pass = offen), Captive Portal (DNS beantwortet alles mit
// der eigenen IP), HTTP und WebSocket unter "/ws". PC: nur HTTP/WebSocket im lokalen Netz.
// false, wenn das Netz nicht startet (PC: Headless-Modus oder Port belegt).
bool start(const char* ssid, const char* pass);
void stop();

struct Info {
  bool     running;
  bool     accessPoint;     // eigener Access Point (ESP32), sonst lokales Netz (PC)
  uint8_t  stations;        // ESP32: verbundene WLAN-Geräte; PC: 0
  uint16_t port;
  char     host[24];        // IP-Adresse für die Anzeige, z. B. "192.168.77.1"
};
Info info();

enum class EventType : uint8_t { Open, Message, Close };
constexpr size_t kMaxMessage = 32;  // längere Nachrichten verwirft die Plattform
struct Event {
  EventType type;
  uint8_t   len;
  ClientId  client;
  uint8_t   data[kMaxMessage];
};
// Ereignisse in Eingangsreihenfolge abholen. Rückgabe: Anzahl in out.
size_t poll(Event* out, size_t max);

// Binäre WebSocket-Nachricht an einen Client. Darf aus der Hauptschleife aufgerufen werden.
bool send(ClientId c, const uint8_t* data, size_t len);

}  // namespace pd::hal::net
