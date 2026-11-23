# pixeldeck - Spezifikation

Stand: 2026-11-22, Version 0.8 (Snake mit 1-4 Spielern und Powerup-Modus, Simulator-Option `--fuzz-players`; Version 0.7: Festlegungen aus M6 eingearbeitet: WebPad mit Access Point, Captive Portal, Protokoll, Controller-Seite, PC-Server; M5 Audio steht noch aus; Version 0.6: Festlegungen aus M4: Slots, Lobby, Profile, Highscores, Tastaturlayouts, Gamepads; Version 0.5: Festlegungen und Messwerte aus M3; Version 0.4: M1 und M2; Version 0.2: Registry per CMake, WebPad-Protokoll im Core und Profilwahl beim Join im laufenden Spiel bestätigt)

pixeldeck ist ein kleines "Betriebssystem" mit Spielen für eine selbstgebaute Spielekonsole (ESP32 + 64x32 RGB-LED-Matrix). Derselbe C++-Code läuft nativ auf dem ESP32 und als Simulator am PC. Dieses Dokument beschreibt alles oberhalb der Hardware-Ansteuerung; die HAL wird hier nur als Schnittstelle spezifiziert.

Kennzeichnung: **[Annahme]** = begründete Festlegung ohne Messung oder ausdrückliche Entscheidung, vor der Umsetzung prüfen. **[Offen]** = noch zu klären.

---

## 1. Hardware-Rahmen

| Komponente | Ausführung |
|---|---|
| MCU | ESP32-D0WD-V3 (Rev. 3.0, 2 Kerne, 240 MHz), 8 MB Flash (gemessen), **kein PSRAM** |
| Display | 64x32 RGB-LED-Matrix mit FM6126A-Treibern, HUB75 direkt an GPIOs, Ansteuerung per I2S-DMA |
| Eingabe | Zweiter Mikrocontroller (ATxmega, "xmega") meldet Tastenzustand per UART |
| Tasten | A, B, X, Y, L, R, Start, Select, D-Pad (4), linker + rechter Analogstick mit L3/R3, Power |
| Power | Akkumessung, Lade-Erkennung und Power-Taste am xmega, Daten per UART (siehe 1.2) |
| USB | einziger Anschluss, am xmega: Laden, USB-CDC-Brücke auf ESP-UART0 (Flashen, Log), DFU für den xmega |
| Ton | noch keine Hardware |

### 1.1 Display-Anschluss (M3)

Pins: R1 32, G1 33, B1 27, R2 14, G2 12, B2 13, A 2, B 0, C 4, D 16, LAT 5, OE 18, CLK 17 (E entfällt, 1/16 Scan).
Treiber `ESP32-HUB75-MatrixPanel-DMA` 3.1.0 (MIT) unverändert, Konfiguration: Double Buffer,
`HZ_10M` (in 3.1.0 = 8 MHz), `clkphase = false`, 8 Bit Farbtiefe, kein CIE-Mapping.
Die FM6126A-Register setzt die HAL vor `begin()` selbst per Bitbang (REG1 = alle Bits 1, REG2 = 0);
mit den Upstream-Werten hatte das Panel einen Rotstich. Der Lib wird deshalb `SHIFTREG` übergeben.
Gamma-Korrektur in der HAL: RGB565 → 8 Bit je Kanal per Tabelle mit `PD_PANEL_GAMMA`
(2,2, am Gerät bestätigt: Testbild sieht aus wie am PC). Der Simulator zeigt die Werte unkorrigiert (sRGB).

### 1.2 xmega-Link (M3)

xmega-Firmware: `platform/pixeldeck/xmega` (aus dem HMatrix-Projekt übernommen; LUFA 170418 als
Teilmenge in `lufa/LUFA`, ATxmega128A4U, 48 MHz),
Flashen per DFU (Joystick beim Einschalten halten, `dfu-programmer atxmega128a4u erase/flash/start`).
In M3 geändert: fester Sendetakt und Power-Taste (siehe unten); Protokoll unverändert.


- ESP-UART0, 115200 Baud. ESP-TX = Konsole/Log (xmega leitet an USB), ESP-RX = Statuspakete des
  xmega, gemischt mit Daten vom USB-Host (Flashen, Dev-Kanal).
- Paket, 10 Byte: `u16 buttons (LE) | u8 lx | u8 ly | u8 rx | u8 ry | u16 Akku-ADC (LE) | crcH | crcL`.
  CRC16 vom xmega-CRC-Modul; Prüfung `~esp_crc16_be(0, paket, 10) == 0`. Der ESP sucht Pakete mit
  einem Schiebefenster über den Byte-Strom (Resync ohne Startbyte).
- Bits: 0 A, 1 B, 2 X, 3 Y, 4 L, 5 R, 6 Select, 7 Start, 8 Links, 9 Hoch, 10 Rechts, 11 Runter,
  12 L3, 13 R3, 14 Laden, 15 Abschalten läuft (gelatcht, siehe unten).
- Rate: **200 Pakete/s**, fester Takt per Timer TCC0 (gemessen 1048 Pakete in 5 s). Vorher alle
  1024 Durchläufe der xmega-Hauptschleife, gemessen ca. 20/s am USB.
- Bitbelegung am Gerät bestätigt (alle 14 Tasten in Sollreihenfolge gedrückt).
- Sticks: Rohwert 1-255, Ruhelage gemessen 127-128; der rechte Stick X zittert in Ruhe um bis
  zu +15. Rechts und unten = größere Werte, passt zur Spielsicht. HAL: `raw - 128`, Totzone
  **[Annahme]** ±16 (knapp über dem gemessenen Zittern), auf ±127 skaliert.
- Akku: `mV = ADC * 4125 / 4095`, 3300 mV = 0 %, 4125 mV = 100 % (Formel aus dem HMatrix-Projekt,
  nicht kalibriert). Am USB-Kabel liefert der ADC 4095.
- Link gilt nach **[Annahme]** 500 ms ohne gültiges Paket als getrennt.
- DTR/RTS des USB-Hosts steuern über den xmega BOOT/EN des ESP. Linux setzt beim Öffnen des
  Ports beide Leitungen, jedes Öffnen startet den ESP also neu.

Power-Taste: Solange die Taste gedrückt ist, hält die Hardware die Versorgung; sonst hält sie der
xmega über `KEEPALIVE`. Ablauf seit M3 (Polling im 200-Hz-Takt, kein Pin-Interrupt mehr):
1. Der Einschalt-Druck zählt nicht, bis die Taste einmal losgelassen wurde.
2. 20 ms gedrückt → Bit 15 gesetzt und gelatcht.
3. Nach **1,5 s Gnadenfrist** `KEEPALIVE` low; das Gerät ist aus, sobald die Taste losgelassen ist.
4. Läuft es 2 s nach dem Loslassen trotzdem weiter (z. B. Versorgung über USB), wieder `KEEPALIVE`
   high und Bit 15 löschen; der Betrieb geht weiter.

Bis M3 setzte ein fehlerhafter Interrupt (ISC per `|=` auf Low-Level) die Taste in eine
Dauer-ISR mit 100-ms-Delays; die Hauptschleife stand, der ESP sah das Power-Bit nie.

---

## 2. Architektur

```
┌──────────────────────────────────────────────────────┐
│ Spiele (games/*)                                     │  nur pd/*.h + erlaubte STL
├──────────────────────────────────────────────────────┤
│ Core / OS (core/)                                    │  plattformunabhängig
│  Hauptschleife, Arena, Gfx, Fonts, Input-Snapshot,   │
│  Slot-Manager, Profile, Scores, Storage, Synth,      │
│  Launcher, Systemmenü, WebPad-Protokoll              │
├──────────────────────────────────────────────────────┤
│ HAL-Schnittstelle (core/include/pd/hal/*.h)          │  nur Deklarationen
├────────────────────────┬─────────────────────────────┤
│ platform/pixeldeck/    │ platform/pc (SDL3)          │  Implementierungen
│   esp32 (IDF)          │                             │
└────────────────────────┴─────────────────────────────┘
```

Grundsätze:
- **Statisch gelinkt:** Alle Spiele werden mit dem OS in ein Binary kompiliert. Kein Nachladen.
- **HAL ist "dumm":** Sie liefert Rohzustand und nimmt Ausgaben entgegen. Jede Logik (Flanken, Slot-Zuordnung, Menüs, Mischen) liegt im Core, damit sie auf allen Plattformen identisch ist.
- **HAL-Bindung zur Link-Zeit:** Freie Funktionen, deklariert im Core, definiert in der Plattform. Keine virtuellen Klassen. Welche Plattform aktiv ist, entscheidet allein, was gelinkt wird.
- **Core und Spiele haben keine Abhängigkeit auf SDL oder ESP-IDF.** Erzwungen über CMake-Targets.

---

## 3. Sprache und Regeln

- **C++17.** Compiler: GCC (Xtensa) für ESP32, GCC/Clang am PC, MSVC optional.
- Verboten bzw. abgeschaltet: Exceptions (`-fno-exceptions`), RTTI (`-fno-rtti`), `<iostream>`.
- Keine Heap-Allokation in Spielen (`new`, `malloc`, allokierende STL-Container). Speicher nur über die Arena (Abschnitt 9). Am PC per Debug-Hook prüfbar.
- Erlaubte STL: header-only und allokationsfrei, z. B. `<cstdint>`, `<array>`, `<algorithm>`, `<cstring>`, `<type_traits>`, `<limits>`.
- Spiele sind single-threaded und laufen im OS-Task.
- `float` ist auf dem ESP32 per FPU schnell, `double` wird in Software emuliert und ist langsam. Spiele sollen `double` meiden.
- Determinismus ist **Empfehlung, keine Pflicht.** Wer nur `ctx.random()` und `ctx.tick()` nutzt, bekommt auf derselben Plattform reproduzierbare Abläufe.

---

## 4. Spiel-API

### 4.1 Lebenszyklus

Das OS besitzt die Hauptschleife. Pro Tick (fest **60 Hz**):

1. HAL-Eingaben lesen, Slot-Manager aktualisieren, Input-Snapshot bilden
2. OS-Kombination prüfen (Start+Select halten auf Slot 0)
3. `game.tick(ctx)` - Logik **und** Zeichnen in einem Aufruf
4. OS-Overlays (Verbindungsverlust, Akkuwarnung) über den Framebuffer legen
5. `hal::display::present(framebuffer)`
6. Bis zum nächsten Tick warten

Schafft ein Spiel 60 Hz nicht, läuft es langsamer. Es werden keine Frames übersprungen. Budget pro Tick: ca. 16,6 ms inklusive OS-Anteil.

```cpp
namespace pd {

class Game {
public:
  virtual ~Game() = default;
  virtual void init(Context& ctx) {}        // einmal nach dem Anlegen; hier ctx.alloc() erlaubt
  virtual void tick(Context& ctx) = 0;      // 60 Hz, Logik + Zeichnen
  virtual void onShutdown(Context& ctx) {}  // vor Beenden oder bei angekündigtem Abschalten; hier speichern
};

}
```

- `onShutdown` wird aufgerufen bei: "Spiel beenden" im Systemmenü, `shutdownPending` von der Power-HAL. Danach wird das Spielobjekt zerstört und die Arena verworfen.
- Während das Systemmenü offen ist, wird `tick()` **nicht** aufgerufen. Spiele brauchen keine eigene Pause-Logik.

### 4.2 Registrierung und Metadaten

```cpp
PD_REGISTER_GAME(Snake, {
  .id          = "snake",           // stabil, [a-z0-9_], max. 12 Zeichen
  .name        = "SNAKE",           // Anzeige im Launcher
  .icon        = assets::icon,      // 16x16 Sprite
  .scoreMode   = ScoreMode::Higher, // None | Higher | Lower
  .minPlayers  = 1,
  .maxPlayers  = 4,
  .saveVersion = 1,
  .stickAsDpad = false,             // linken Stick zusätzlich als D-Pad melden
});
```

- `sizeof(Game)` wird per `static_assert` gegen `PD_ARENA_SIZE` geprüft. Zu großes Spiel = Compile-Fehler.
- Die `id` muss **gleich dem Ordnernamen** unter `games/` sein. CMake übergibt den Ordnernamen als `PD_GAME_SYMBOL`, daraus benennt das Makro die Registrierungsfunktion (`pd_game_entry_<id>`); Abweichung = Compile-Fehler. Das Makro steht im globalen Namensraum.
- Die Länge von `id` wird zur Compile-Zeit geprüft (NVS-Namespaces sind auf 15 Zeichen begrenzt, 3 Zeichen sind für OS-Präfixe reserviert).
- **[Annahme]** Designated Initializers sind formal C++20; GCC/Clang akzeptieren sie in C++17 als Erweiterung. Falls MSVC (Windows) relevant wird, auf Konstruktor- oder Builder-Syntax ausweichen.
- Spiele werden **nicht** über statische Initialisierer registriert, weil der Linker unreferenzierte Objekte aus statischen Bibliotheken verwirft. Stattdessen erzeugt CMake aus allen Spielordnern eine Datei `game_registry.cpp`, die jede Registrierung explizit referenziert. Das Makro erzeugt dafür eine Funktion mit festem Namensschema.

### 4.3 Context

```cpp
class Context {
public:
  Gfx&     gfx;
  Input&   input;
  Audio&   audio;
  Storage& storage;
  Scores&  scores;

  uint32_t tick() const;                    // Ticks seit Spielstart, ohne Pausen
  uint32_t random();                        // OS-PRNG (PCG32 oder xorshift32)
  uint32_t random(uint32_t maxExclusive);
  void*    alloc(size_t size, size_t align = alignof(std::max_align_t)); // nur in init()
  void     log(const char* fmt, ...);
};
```

- `ctx.tick()` ist die **einzige Zeitquelle** für Spiele. `uint32_t` reicht bei 60 Hz für ca. 2,2 Jahre.
- Der PRNG wird bei jedem Spielstart aus `hal::random::seed()` initialisiert. Am PC per `--seed` festlegbar.

---

## 5. Grafik

### 5.1 Framebuffer

- 64x32 Pixel, **RGB565** (`uint16_t`, native Byte-Reihenfolge), 4 KB.
- Gehört dem OS. Das Spiel zeichnet in `tick()` hinein, das OS übergibt ihn danach an `hal::display::present()`.
- **Kein automatisches Löschen.** Wer neu zeichnen will, ruft `gfx.clear()`.
- Gamma-Korrektur und Helligkeit sind Sache der HAL.

### 5.2 Farben

```cpp
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t TRANSPARENT = 0xF81F;   // Magenta, Farbschlüssel für Sprites
```

Reines Magenta ist damit in Sprites nicht als sichtbare Farbe nutzbar.

### 5.3 Zeichenfunktionen

Alle Funktionen clippen. Koordinaten außerhalb des Bildschirms sind erlaubt.

```cpp
class Gfx {
public:
  static constexpr int W = 64, H = 32;

  void     clear(uint16_t c = 0);
  void     pixel(int x, int y, uint16_t c);
  uint16_t get(int x, int y) const;         // außerhalb: 0

  void hline(int x, int y, int w, uint16_t c);
  void vline(int x, int y, int h, uint16_t c);
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  void rect(int x, int y, int w, int h, uint16_t c);
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void circle(int cx, int cy, int r, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);

  void blit(const Sprite& s, int x, int y, uint8_t flags = 0);  // FLIP_X, FLIP_Y

  int  text(int x, int y, const char* str, uint16_t c, Font f = Font::Small); // gibt Breite zurück
  int  textWidth(const char* str, Font f = Font::Small) const;

  uint16_t* buffer();                       // direkter Zugriff, Zeilen hintereinander
};
```

Nicht enthalten: Tilemaps, Rotation, Skalierung, Alpha-Blending, Kamera/Scroll-Offset.

### 5.4 Sprites

```cpp
struct Sprite {
  uint8_t w, h;
  uint16_t frames;
  const uint16_t* data;                     // im Flash, frames * w * h Pixel
  Sprite frame(uint16_t n) const;
};
```

### 5.5 Fonts

| Font | Glyphe | Zeichen pro Zeile x Zeilen (inkl. 1 px Abstand) |
|---|---|---|
| `Font::Small` | 3x5 | 16 x 5 |
| `Font::Large` | 5x7 | 10 x 4 |

- Zeichensatz ASCII 32-126. Umlaute nur bei Bedarf nachrüsten.
- `Font::Small` hat **keine Kleinbuchstaben**: a-z werden als A-Z gezeichnet. `Font::Large` hat eigene Kleinbuchstaben.
- Beide Fonts sind im Projekt selbst gepixelt (`core/src/font_data.cpp`), keine Fremdvorlage.

---

## 6. Eingabe

### 6.1 Buttons und Sticks

16 Buttons in einer `uint16_t`-Bitmaske:

```cpp
enum class Button : uint16_t {
  A = 1 << 0,  B = 1 << 1,  X = 1 << 2,  Y = 1 << 3,
  L = 1 << 4,  R = 1 << 5,  Start = 1 << 6, Select = 1 << 7,
  Up = 1 << 8, Down = 1 << 9, Left = 1 << 10, Right = 1 << 11,
  L3 = 1 << 12, R3 = 1 << 13,
  // Bits 14, 15 reserviert
};

struct Stick { int8_t x, y; };              // -127..127, Deadzone/Kalibrierung bereits angewendet
                                            // Achsen wie der Bildschirm: x > 0 rechts, y > 0 unten
```

### 6.2 Spielersicht

```cpp
class Player {
public:
  bool connected() const;
  bool held(Button b) const;
  bool pressed(Button b) const;             // in diesem Tick neu gedrückt
  bool released(Button b) const;            // in diesem Tick losgelassen
  uint16_t buttons() const;                 // Rohmaske aller gehaltenen Buttons
  Stick left() const;
  Stick right() const;
  const char* name() const;                 // Profilname oder "GAST"
  uint16_t color() const;                   // Profilfarbe
  void rumble(uint16_t ms);                 // No-Op, wenn das Gerät es nicht kann
};

class Input {
public:
  static constexpr int MAX_PLAYERS = 4;
  Player& player(int slot);
  // Kurzformen für Slot 0:
  bool held(Button b) const;
  bool pressed(Button b) const;
  bool released(Button b) const;
};
```

- **Snapshot:** Vor jedem `tick()` gebildet, während des Ticks konstant.
- Spiele kennen den **Controller-Typ nicht**. Das OS kennt ihn (für Icons im Menü).
- **Start+Select halten (ca. 1 s) am Primärgerät** (eingebautes Pad bzw. Tastatur 1, normalerweise Slot 0) öffnet das Systemmenü. Diese Kombination sieht das Spiel nie. An anderen Geräten ohne OS-Funktion. Technisch: Sind beide gleichzeitig gehalten, blendet das OS beide aus, bis beide losgelassen sind; ein vorher allein gedrücktes Start erreicht das Spiel also kurz.
- Slots oberhalb von `maxPlayers` sind für das Spiel immer `connected() == false`.
- Mit `stickAsDpad` meldet das OS den linken Stick zusätzlich als D-Pad-Bits (Schwelle **[Annahme]** 50 % Auslenkung).

### 6.3 Geräte und Slot-Manager

- Quellen: eingebautes Pad (UART), WebPad (WLAN), am PC Tastatur (zwei Layouts) und SDL-Gamepads.
- Jedes Gerät hat eine stabile ID, einen Typ (`Builtin`, `Keyboard`, `Gamepad`, `WebPad`) und einen Anzeigenamen.
- **Slot 0** ist fest das eingebaute Pad, am PC die Tastatur (Layout 1).
- Weitere Geräte belegen einen Slot erst nach **"Press A to join"**, und zwar den ersten freien.
- **Trennung:** Slot bleibt für `PD_SLOT_HOLD_MS` (**[Annahme]** 10 s) reserviert. Kommt dasselbe Gerät (gleiche ID) zurück, bekommt es seinen Slot wieder. WebPad-ID = Token aus `localStorage` des Browsers.
- Bei Trennung während eines Spiels: **kein Auto-Pause**, nur kurzes Overlay "P2 GETRENNT".
- Slots tauschen im Systemmenü.

Umgesetzt (M4), `core/src/input_system.*`:
- **Primärgerät** = erstes Gerät vom Typ `Builtin` oder `Keyboard` in der HAL-Liste. Es bekommt ohne A einen Slot (Slot 0, wenn frei) und bedient immer das OS (Launcher, Lobby, Menüs, Start+Select), auch wenn es per Tausch in einem anderen Slot sitzt. Es lässt sich nicht aus dem Slot entfernen.
- Das A, mit dem ein Gerät beitritt, erreicht weder Spiel noch OS (ausgeblendet, bis losgelassen). Frisch verbundene Geräte erzeugen keine Flanken für bereits gehaltene Tasten.
- Kanten (`pressed`/`released`) werden pro Gerät gebildet, nicht pro Slot; ein Tausch erzeugt keine Phantom-Flanken.
- Reservierung zählt in Ticks (`PD_SLOT_HOLD_MS` bei 60 Hz). Während der Reservierung ist der Slot für das Spiel `connected() == false`, Name und Farbe bleiben.
- Overlays (12.3): "P2 GETRENNT", "P2 ZURUECK", bei Join im Spiel "P2 <NAME>".
- WebPads (M6) sind Geräte vom Typ `WebPad`, die der Core selbst erzeugt und hinter den HAL-Geräten in den Slot-Manager speist (`MAX_DEVICES` = 16). Beitritt wie ein Gamepad per A, Profil kommt vom Handy (7.1), keine Profilwahl auf der Matrix; im Spiel und außerhalb erscheint "P2 <NAME>".

### 6.4 Lobby

Startet ein Spiel mit `minPlayers` > verbundenen Spielern, zeigt das OS zuerst eine Lobby ("PRESS A TO JOIN 1/2"). Start, sobald genug Spieler da sind. Spieler können auch während des Spiels joinen; ob das Spiel sie berücksichtigt, entscheidet es selbst.

Umgesetzt (M4): Anzeige Spielname, "A: MITSPIELEN", "n/minPlayers" und je Slot bis `maxPlayers` ein Kästchen in Profilfarbe. Gezählt werden verbundene Slots unterhalb von `maxPlayers`. B am Primärgerät bricht ab und führt in den Launcher. Gilt auch für den Direktstart (`--game`). Gestartet wird erst nach der Profilwahl des neuen Spielers.

---

## 7. Profile und Highscores

### 7.1 Profile

- Lokal auf der Konsole gespeichert, kein Netz-Account. Max. **16 Profile**.
- Profil = **Name** (max. 8 Zeichen) + **Farbe** aus fester Palette (**[Annahme]** 8 Farben).
- **Gast** gibt es immer, Name "GAST".
- Anlegen, Umbenennen, Löschen im Systemmenü, ohne Schutz.
- Auswahl beim Join:
  - **WebPad:** Auswahl oder Neuanlage auf dem Handy. Handy merkt sich das Profil, danach genügt A.
  - **Eingebautes Pad / Gamepad / Tastatur:** Kurze Profilliste auf der Matrix, vorausgewählt das zuletzt genutzte Profil des Geräts.
  - Join **während eines laufenden Spiels** an Konsole oder Gamepad übernimmt ohne Auswahl das letzte Profil des Geräts (sonst Gast). Ändern über das Systemmenü. Grund: kein Auswahlbildschirm mitten im Spiel.

Umgesetzt (M4), `core/src/profiles.*`:
- Ablage `os/profiles` (ein Blob, ca. 300 Byte): Profile in Anlagereihenfolge und pro Gerät (max. 8, älteste fallen heraus) das zuletzt genutzte Profil.
- Profil-ID 1-254, Gast = `0xFF` (wie im WebPad-Protokoll). Neue IDs werden fortlaufend vergeben, gelöschte also erst nach einem Umlauf wieder, damit Highscore-Einträge nicht sofort einem neuen Profil zugeordnet werden.
- Namen: Großbuchstaben, Ziffern, Leerzeichen, "-"; Leerzeichen am Ende werden entfernt, leere Namen abgelehnt.
- Farbpalette **[Annahme]**: Rot, Orange, Gelb, Grün, Cyan, Blau, Violett, Pink. Gast ist weiß.
- Ein Profil kann nur in einem Slot gleichzeitig sein. Ist das zuletzt genutzte Profil schon vergeben, spielt das Gerät als Gast.
- Profilwahl nach dem Join (außerhalb eines Spiels): modaler Bildschirm "P2: WER BIST DU?", bedient vom beitretenden Gerät. Links/Rechts (oder Hoch/Runter) wählt zwischen Gast und freien Profilen, A bestätigt und merkt sich die Wahl für das Gerät, B (dort oder am Primärgerät) bricht den Join ab. Mehrere Joins werden nacheinander abgefragt. Ein neues Profil lässt sich dort nicht anlegen, nur im Systemmenü.
- Das Primärgerät startet mit seinem zuletzt genutzten Profil, ohne Auswahl.
- WebPad (M6): Das Handy schickt sein Profil mit HELLO und bei Änderung (PROFILE_SELECT); beim Beitritt übernimmt der Slot es, wenn kein anderer Slot es nutzt (sonst Gast). Profile lassen sich auf dem Handy anlegen (PROFILE_CREATE). Ändert jemand im Systemmenü das Profil eines WebPad-Slots, übernimmt das Handy es (PROFILE_SET). Die Konsole merkt sich für WebPads kein Profil pro Gerät; das macht der Browser.

### 7.2 Highscores

- Nur für Spiele mit `scoreMode != None`. `Higher` oder `Lower` ist besser.
- `ctx.scores.submit(slot, int32_t score)` ordnet den Score dem Profil im Slot zu. Keine Namenseingabe im Spiel.
- Top 10 pro Spiel. Eintrag = Profil-ID, **Kopie des Namens**, Score. **[Annahme]** Die Namenskopie sorgt dafür, dass Einträge gelöschter Profile erhalten bleiben.
- Gast-Scores werden gespeichert (als "GAST").
- Launcher zeigt den Rekord pro Spiel. Systemmenü: Listen ansehen, pro Spiel zurücksetzen.

Umgesetzt (M4), `core/src/score_table.*`:
- Ablage: Namensraum `s_<id>`, Schlüssel `top`, 164 Byte. Geschrieben wird bei jedem `submit()`, der in die Liste kommt.
- Bei Gleichstand steht der ältere Eintrag vorn.
- **[Annahme]** Bei `Higher` zählen nur Scores > 0 (sonst füllen 0-Punkte-Runden die Liste). Bei `Lower` zählt jeder Wert.
- Slot ohne Gerät oder außerhalb der Slots: Eintrag als Gast.

---

## 8. Storage

### 8.1 Spiel-API

```cpp
class Storage {
public:
  // Lädt den Spielstand. false, wenn nicht vorhanden, Größe abweicht
  // oder saveVersion nicht passt.
  bool load(void* buf, size_t len);
  bool save(const void* buf, size_t len);   // max. PD_SAVE_MAX (4 KB)
  void erase();
};
```

- Jedes Spiel speichert **einen Blob** in seinem Namensraum `g_<id>`, Schlüssel `save`. Zugriff auf fremde Daten nicht möglich.
- Vor den Daten liegt ein 8-Byte-Kopf (Kennung, `saveVersion`, Länge). Das OS braucht dafür einen statischen Puffer von `PD_SAVE_MAX` + 8 Byte.
- Versionierung über `saveVersion` aus den Metadaten: Erhöhen invalidiert alte Stände.
- Geschrieben wird nur bei `save()`. Typische Stelle: `onShutdown()`.

### 8.2 Ablage

| Plattform | Ort |
|---|---|
| ESP32 | NVS (Wear-Leveling durch IDF), Partition `nvs` 128 KB (M3), ein Blob pro Schlüssel |
| PC | `~/.pixeldeck/` (Linux), `%APPDATA%\pixeldeck\` (Windows) |

OS-Daten (Profile, Scores, Einstellungen) liegen in reservierten Namensräumen ohne `g_`-Präfix: Einstellungen in `os/settings` (Version 2 seit M6: zusätzlich WebPad an/aus, SSID-Suffix, WLAN-Passwort; Version 1 wird übernommen), Profile in `os/profiles`, Highscores in `s_<id>/top`. Namensräume und Schlüssel: nur `[a-z0-9_]`, max. 15 Zeichen. Am PC ist das Verzeichnis per `--data-dir` wählbar; geschrieben wird atomar (temporäre Datei, dann umbenennen).

---

## 9. Speicher

- Statische **Arena** im `.bss`, Größe `PD_ARENA_SIZE` (Konfigurationswert, Startwert 64 KB, **seit M6 32 KB**: die Spiele brauchen max. 3,9 KB (Snake mit 4 Schlangen, vorher 1,3 KB), das WLAN braucht den Heap).
- Spielobjekt per Placement-new an den Anfang der Arena.
- `ctx.alloc()` = Bump-Allocator auf dem Rest, nur in `init()`. Kein `free`.
- Beim Beenden: Destruktor, Arena komplett verworfen. Keine Lecks und keine Fragmentierung zwischen Spielen.
- Der **PC-Build verwendet dieselbe Arena-Größe** und meldet den Höchststand.
- Sprites, Fonts, Soundeffekte liegen als `const` im Flash und zählen nicht zur Arena.

Grobe RAM-Bilanz ESP32 **[Annahme, nicht gemessen]**:

| Posten | ca. |
|---|---|
| Nutzbares DRAM nach IDF-Start | 250-290 KB |
| HUB75-DMA-Puffer | 20-30 KB |
| WLAN + WebSocket (nur wenn aktiv) | gemessen M6: 26 KB statisch, ca. 54-65 KB Heap |
| OS, Framebuffer, Stacks, Puffer | 30-40 KB |
| Arena | 32 KB (bis M6: 64 KB) |

---

## 10. Audio

Hardware fehlt noch. Die Spiel-API ist trotzdem festgelegt, damit Spiele schon Ton benutzen können; am PC funktioniert sie über SDL.

- **Synth im Core**, 4 Kanäle: 2x Rechteck (einstellbare Pulsbreite), 1x Dreieck, 1x Rauschen.
- Version 1: **nur Soundeffekte.** Musik (Tracker-Format) kommt später; die Kanal-API wird so gebaut, dass Musik ohne Bruch aufsetzt (z. B. ein Kanal für Effekte reservierbar).
- Spiele schreiben Befehle in eine Queue; das Mischen läuft in einem eigenen Task.
- Ausgabe an die HAL: **22 050 Hz, mono, `int16_t`**.
- Globale Lautstärke und Stummschaltung im Systemmenü.

```cpp
struct Sfx {                                // Skizze, Details bei M5
  Wave     wave;                            // Square, Triangle, Noise
  uint16_t freqStart, freqEnd;              // Hz, linearer Sweep
  uint16_t durationMs;
  uint8_t  volume;                          // 0-255
  uint8_t  duty;                            // nur Square
  uint8_t  attackMs, releaseMs;
};

class Audio {
public:
  void play(const Sfx& sfx, int channel = -1);  // -1: OS wählt passenden Kanal
  void stop(int channel);
};
```

---

## 11. HAL-Schnittstelle

Alle Funktionen in `core/include/pd/hal/`, Namespace `pd::hal`. Die Plattform muss jede definieren. Signaturen sind ein Entwurf und dürfen bei der Umsetzung noch präzisiert werden.

```cpp
namespace pd::hal {

namespace display {
  void init();
  void present(const uint16_t* fb);         // 64*32 RGB565, Zeile für Zeile
  void setBrightness(uint8_t level);        // 0-255
}

namespace input {
  enum class DeviceType : uint8_t { Builtin, Keyboard, Gamepad, WebPad };  // WebPad nur vom Core
  struct RawInput { uint16_t buttons; int8_t lx, ly, rx, ry; };  // kalibriert, mit Deadzone
  struct Device {
    uint32_t   id;                          // stabil über Neuverbindungen
    DeviceType type;
    char       name[16];
    bool       connected;
    RawInput   state;
  };
  size_t poll(Device* out, size_t max);     // alle bekannten Hardware-Geräte
  void   rumble(uint32_t id, uint16_t ms);  // darf No-Op sein
}

namespace power {
  struct PowerState {
    bool     available;                     // Akku vorhanden/gemessen (PC: false)
    uint16_t millivolts;
    uint8_t  percent;
    bool     charging;
    bool     shutdownPending;               // Power-Taste gedrückt, Abschalten steht bevor
  };
  PowerState read();
  void shutdownReady();                     // OS hat gesichert, darf abschalten
}

namespace time {
  uint64_t micros();                        // monoton
  void     sleepUntil(uint64_t micros);
}

namespace storage {
  bool load(const char* ns, const char* key, void* buf, size_t len, size_t* outLen);
  bool save(const char* ns, const char* key, const void* buf, size_t len);
  bool erase(const char* ns, const char* key);
}

namespace log {
  enum class Level : uint8_t { Debug, Info, Warn, Error };
  void write(Level lvl, const char* msg);   // Formatierung macht der Core
}

namespace random {
  uint32_t seed();                          // echter Zufall
}

namespace app {
  bool quitRequested();                     // PC: Fenster geschlossen oder Esc; ESP32: immer false
  void heartbeat();                         // einmal pro Hauptschleife; ESP32: Task-Watchdog
  void setActivity(const char* gameId);     // laufendes Spiel (nullptr = OS), übersteht Reset
  enum class ResetCause : uint8_t { None, Hang, Crash };
  struct LastReset { ResetCause cause; char gameId[13]; };
  LastReset lastReset();                    // Grund des letzten Neustarts, einmal nach Boot
}

namespace audio {                           // reserviert
  void   init(uint32_t sampleRate);
  size_t write(const int16_t* samples, size_t n);  // darf blockieren; ESP vorerst No-Op
}

namespace net {                             // Transport für WebPad, Protokoll liegt im Core (M6)
  using ClientId = uint32_t;
  void serveStatic(const char* path, const uint8_t* gz, size_t len, const char* mime);
  bool start(const char* ssid, const char* pass);  // ESP: AP + Captive Portal + HTTP/WS; PC: HTTP/WS
  void stop();
  struct Info { bool running, accessPoint; uint8_t stations; uint16_t port; char host[24]; };
  Info info();
  enum class EventType : uint8_t { Open, Message, Close };
  struct Event { EventType type; uint8_t len; ClientId client; uint8_t data[32]; };
  size_t poll(Event* out, size_t max);      // Ereignisse abholen, einmal pro Tick
  bool send(ClientId c, const uint8_t* data, size_t len);
}

}
```

Hinweise:
- `input::poll` liefert nur Hardware-Geräte. WebPad-Geräte erzeugt der Core aus dem Protokoll und speist sie in denselben Slot-Manager. Die HAL stellt für das WebPad nur den Transport bereit (`net`), das Protokoll ist plattformunabhängig im Core.
- `net` (M6): statt der ursprünglich geplanten Callbacks legt die Plattform Ereignisse (Verbindung auf, Nachricht, Verbindung zu) in eine gesperrte Warteschlange, die der Core einmal pro Tick mit `poll()` leert. So bleibt der Core single-threaded. Nachrichten über 32 Byte verwirft die Plattform. Captive Portal ist Teil von `start()` statt eigener Funktion.
- UART-Protokoll zum xmega: Abschnitt 1.2. Es bestimmt nur die ESP-Implementierung von `input` und `power`.
- ESP32: `input::poll` liefert ein Gerät `Builtin` "KONSOLE", verbunden, solange Pakete kommen.
  `power::read().shutdownPending` = Power-Bit, aber erst nachdem die Taste nach dem Einschalten
  einmal losgelassen wurde (sonst löst der Einschalt-Druck sofort den Shutdown aus).

---

## 12. OS-Oberfläche

Launcher und Systemmenü sind intern selbst Spiele über dieselbe API (Test der API, dieselbe Grafikbibliothek).

### 12.1 Launcher

- Karussell: ein Spiel pro Ansicht. Icon 16x16 links, rechts Name (5x7), darunter Rekord + Profilname (3x5) und Spielerzahl ("1-4P").
- Links/Rechts (D-Pad oder Stick) blättert, A startet, Start öffnet das Systemmenü.
- Statuszeile (3x5): Akkustand, WebPad an/aus, belegte Slots als farbige Punkte. Umgesetzt (M2): Position "n/m" links, Slot-Punkte rechts; WebPad ab M6. Akku (M3): Batteriesymbol links der Slot-Punkte, grün, unter 10 % rot, beim Laden gelb; nur wenn `available`. Slot-Punkte (M4) in Profilfarbe, reservierte Slots blinken. WebPad (M6): "W" links vom Akku, blau wenn an, cyan sobald ein Handy verbunden ist. Rekord (M4): Score in Gelb, dahinter der Profilname, wenn er noch in die Zeile passt; ohne Eintrag "REK ---".
- Der Launcher merkt sich das zuletzt gestartete Spiel (OS-Einstellungen) und wählt es beim nächsten Mal vor.
- Namen bis 38 px Breite (6 Zeichen) in 5x7, längere in 3x5.

### 12.2 Systemmenü

Aus dem Launcher per Start, im Spiel per Start+Select halten (Slot 0). Im Spiel liegt das Menü über dem **abgedunkelten, eingefrorenen Spielbild**.

Umgesetzt (M2): Weiter, Beenden, Helligkeit; aus dem Launcher Helligkeit und Zurück. Bedienung: Hoch/Runter wählen, A ausführen, Links/Rechts Helligkeit, B oder Start schließt, Start+Select schließt ebenfalls. Das Bild wird beim Öffnen einmal im Framebuffer abgedunkelt (kein zweiter 4-KB-Puffer); ein Spiel, das nicht selbst `clear()` aufruft, sieht nach "Weiter" Reste davon. Helligkeit: 8 Stufen **[Annahme]** (12, 24, 44, 72, 108, 152, 204, 255), gespeichert in den OS-Einstellungen. "Beenden" führt immer in den Launcher, auch nach Direktstart.

Umgesetzt (M4): Hauptliste im Spiel WEITER, BEENDEN, SPIELER, PROFILE, REKORDE, HELL; im Launcher SPIELER, PROFILE, REKORDE, HELL, ZURUECK. Drei Zeilen sichtbar, das Fenster rollt mit, Rollbalken rechts. Start schließt das Menü von jeder Seite, B geht eine Ebene zurück. Die Unterseiten füllen den ganzen Bildschirm; nach "Weiter" sieht ein Spiel ohne `clear()` deren Reste.
- **SPIELER:** vier Zeilen Slot, Profilfarbe, Name, Gerät (KON, TAS, PAD; reserviert blinkt). Hoch/Runter Slot, Links/Rechts Profil (Gast und freie Profile, wird für das Gerät gemerkt), A markiert, zweites A tauscht, X entfernt das Gerät aus dem Slot (nicht das Primärgerät).
- **PROFILE:** Liste mit "+ NEU" am Ende, Anzahl "n/16". A öffnet den Editor: Hoch/Runter Zeichen, Links/Rechts Cursor, X Farbe, A speichern, B abbrechen, Y löschen mit Rückfrage "LOESCHEN? A JA B NEIN". Gelöschte Profile werden in Slots und Gerätezuordnung zu Gast.
- **REKORDE:** Links/Rechts Spiel (nur mit `scoreMode`), vorgewählt das zuletzt gestartete, vier Einträge sichtbar, Hoch/Runter blättert, X setzt nach Rückfrage zurück.

Umgesetzt (M6): Menüpunkt **WEBPAD** zwischen REKORDE und HELL (grüner Punkt, wenn an). Die Seite zeigt "AN n" (verbundene Handys) und am ESP SSID, Passwort und 192.168.77.1, am PC IP und Port. A schaltet ein/aus (gespeichert, gilt auch nach Neustart), X erzeugt ein neues WLAN-Passwort (Access Point startet neu), B zurück. Scheitert der Start, steht dort "NETZ FEHLER".

Gesamtumfang:
- Nur im Spiel: **Weiter**, **Spiel beenden**
- **Spieler:** Slots mit Controller-Icon, tauschen, Profil wechseln
- **Profile:** anlegen, umbenennen, löschen
- **WebPad:** an/aus, SSID + Passwort (umgesetzt M6, als Text), QR-Code (umgesetzt nach M6, siehe unten)

QR-Ansicht (nach M6): Y auf der WEBPAD-Seite. Links der Code mit 1 px pro Modul und 1 Modul hellem Rand, rechts die Erklärung. Schritt 1/2 "WLAN MIT KAMERA": `WIFI:T:WPA;S:PIXELDECK-XXXX;P:<pw>;;` (Byte-Modus, Version 3, 29x29, mit Rand 31 px hoch). Schritt 2/2 "SEITE IM BROWSER": `HTTP://192.168.77.1` in Großbuchstaben, damit alphanumerisch und Version 1 (21x21). Wechsel mit Links/Rechts, sonst automatisch alle 8 s; B oder Y zurück. Am PC nur die Adresse mit Port. Helle Module leuchten in Grau (170, 170, 170) **[Annahme]**, damit die Kamera nicht überstrahlt. Der Seiten-Code öffnet die Seite direkt im normalen Browser, unter iOS also in Safari statt im Anmeldefenster, das nicht ins Querformat dreht.
Encoder `core/src/qrcode.*`: eigene Umsetzung nach ISO/IEC 18004, Version 1-4, Fehlerkorrektur L (je ein Reed-Solomon-Block), alphanumerisch oder Byte, Maskenwahl nach Strafpunkten, kein Heap. Geprüft mit `zbarimg` (CTest `qr_zbar`, nur wenn zbar installiert ist) und am Gerät über den Framebuffer (`pd_serial.py fb` + `zbarimg`, beide Codes gelesen). Mit der Handykamera direkt von der Matrix lesbar (vom Nutzer am Gerät bestätigt).
- **Helligkeit**, **Lautstärke**
- **Highscores:** ansehen, pro Spiel zurücksetzen
- **Info:** Firmware-Version, freier Heap, Arena-Höchststand, Tick-Dauer (Durchschnitt/Maximum), Akku in mV

### 12.3 Overlays

- Spieler getrennt / verbunden (kurz, oben). Umgesetzt (M4): 2 s, Streifen 64x7 oben; der überdeckte Bildteil wird nach `present()` zurückgeschrieben, Spiele sehen das Overlay also nie im Framebuffer.
- Akku niedrig **[Annahme]** unter 10 %
- Hinweis nach Absturz oder Watchdog-Reset (siehe 14.2)

---

## 13. WebPad

Optional zuschaltbar im Systemmenü (RAM und Akku). Läuft auf ESP und PC.

### 13.1 Netz und Seite

- ESP spannt einen eigenen **Access Point** auf (SSID z. B. `PIXELDECK-XXXX`). Kein Heim-WLAN in Version 1.
- **Captive Portal:** DNS beantwortet alle Anfragen mit der eigenen IP, Controller-Seite öffnet sich auf den meisten Handys automatisch.
- Eine HTML-Datei mit inline JS/CSS, gzip-komprimiert im Flash, Ziel < 20 KB.
- Querformat, Nintendo-Layout: zwei virtuelle Sticks, D-Pad, ABXY, L/R, Start/Select. L3/R3 per Doppeltipp auf den Stick. Multitouch.
- Profilauswahl und -anlage auf dem Handy; Token und Profil im `localStorage`.
- Rumble über die Vibration-API des Browsers. **[Annahme]** funktioniert auf Android, nicht auf iOS Safari; nicht aktuell geprüft.
- Passwortschema **[Annahme, M6]**: WPA2, pro Gerät einmal zufällig erzeugt (8 Ziffern, leicht am Handy einzutippen) und in `os/settings` gespeichert, SSID `PIXELDECK-XXXX` mit zufälligem 4-stelligem Hex-Suffix. Neues Passwort im Systemmenü. Wer das WLAN offen will, kann `start()` ein leeres Passwort geben.

Umgesetzt (M6):
- ESP32 (`platform/pixeldeck/esp32/main/hal_net.cpp`): eigenes Netz 192.168.77.0/24 **[Annahme]** statt des ESP-Standards 192.168.4.0/24 (kollidierte mit dem Büronetz 192.168.0.0/21 des Testrechners). Access Point auf Kanal 6 **[Annahme]**, max. 4 WLAN-Geräte **[Annahme]**, kein Stromsparmodus. DHCP bietet die eigene IP als DNS und die Captive-Portal-URL (RFC 8910, Option 114) an. Ein kleiner DNS-Task beantwortet jede A-Anfrage mit 192.168.77.1 (AAAA leer). `esp_http_server` liefert die Seite unter "/" (gzip), WebSocket unter "/ws", alle anderen URLs (z. B. `generate_204`, `hotspot-detect.html`) per 302 auf die Seite. Gesendet wird per `httpd_queue_work` im HTTP-Task, der Frame wird selbst gebaut und in einem Stück geschrieben (`httpd_socket_send`), WebSockets mit `TCP_NODELAY`; `httpd_ws_send_frame_async` schreibt Kopf und Nutzdaten getrennt, mit Nagle wartete der zweite Teil auf die Bestätigung. Sockets: `CONFIG_LWIP_MAX_SOCKETS` 16, davon httpd 12 (+3 intern) und 1 DNS; normale HTTP-Antworten schließen die Verbindung sofort (`Connection: close`). Mit den IDF-Vorgaben (10 Sockets, Keep-Alive) war die Tabelle schon mit zwei Handys voll (`accept`: ENFILE), weitere Handys kamen nicht mehr durch. `stop()` baut WLAN komplett ab (`esp_wifi_deinit`).
- PC (`platform/pc/net_pc.cpp`): HTTP/WebSocket-Server im lokalen Netz, Port 8080 (`--webpad-port`), eigener Thread ohne Heap-Allokationen (sonst meldet der Heap-Hook Fehlalarme). Gleiche 302-Umleitung wie am ESP. Kein Access Point. Windows: nicht umgesetzt.
- Seite `web/webpad.html` (eine Datei, kein Framework), beim Build von `tools/pd_embed.py` von Einrückungen befreit, gzip-komprimiert und als Array in `pd_core` eingebettet (ca. 5 KB). Aufbau quer: L/R oben, dazwischen − (Select), Status, + (Start), darunter PROFIL; links Stick oben und D-Pad unten, rechts ABXY oben und Stick unten. Über Tasten gleiten wechselt die Taste, das D-Pad erkennt 8 Richtungen, Doppeltipp auf einen Stick hält L3/R3. Vollbild und Querformat-Sperre bei jedem Loslassen eines Fingers, solange kein Vollbild aktiv ist (bei Touch gilt erst `pointerup`/`click` als Nutzeraktion, nicht `pointerdown`). Das Layout rechnet mit der sichtbaren Höhe (`--vh` aus `innerHeight`), damit bei eingeblendeter Adressleiste nichts verdeckt wird. iPhone hat keine Fullscreen-API: Hinweis "Teilen → Zum Home-Bildschirm" in der Profilauswahl. Hochkant erscheint "Bitte quer halten".
- Erster Aufruf ohne gespeichertes Profil: Auswahl "WER SPIELT?" (Gast, Profile der Konsole, "+ NEU" mit Name und Farbe). Token (16 Byte, hex) und Profil-ID liegen im `localStorage`. Statuszeile auf dem Handy: "P2 NAME" in Profilfarbe, sonst "A DRÜCKEN ZUM MITSPIELEN".
- Trennung und Rückkehr wie bei anderen Geräten (6.3); dasselbe Handy in einem zweiten Tab übernimmt die Verbindung, der alte Tab zeigt "Anderswo verbunden".
- Kurze Drücke zwischen zwei Ticks hält der Core für einen Tick fest, damit sie nicht verloren gehen.

Gemessen M6 (ESP32): Heap frei vor WLAN 107 KB, mit Access Point 42 KB (größter Block 40 KB), nach dem Ausschalten 96 KB; ab dem zweiten Ein/Aus kein weiterer Verlust. Statisches DRAM durch den WLAN-Stack +26 KB (122 KB von 177 KB). 

Messung mit zweiter WLAN-Karte am PC (rtw88-USB, ohne Default-Route), simulierte Handys mit dem Verkehr der Seite (`load.mjs`-Skript, nicht im Repo), nach TCP_NODELAY und kleineren WLAN-/TCP-Puffern (`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` 6, dynamische RX/TX 16, kein AMPDU-TX, `TCP_SND_BUF`/`TCP_WND` 2880):
| | 1 Client | 3 Clients (eine Karte) |
|---|---|---|
| Antwortzeit PING/PONG Median / p95 | 40 / 106 ms (vorher 82 / 194 ms) | 100-180 / 440-510 ms |
| größte Empfangslücke im Betrieb | 116-132 ms | 114-137 ms |
| Heap frei, Minimum | 30 KB | 11 KB |
| CPU Kern 0 / Kern 1 | 10-13 % / 30 % | 17-30 % / 32-37 % |

ICMP-Ping zur Konsole über dieselbe Karte schwankt schon 5-200 ms; die Antwortzeiten mit 3 Clients sind deshalb eher eine Grenze der Testkarte. Heap mit Access Point ohne Clients: 43 KB. Die Spiele nutzen höchstens 1,3 KB der 64-KB-Arena (snake). Nach Halbierung der Arena auf 32 KB: Heap mit Access Point 76 KB, Minimum mit 3 Clients 54 KB (vorher 11 KB); statisches DRAM 91 KB von 177 KB.

### 13.2 Protokoll (Version 1, M6)

WebSocket unter `/ws`, **binär, zustandsbasiert**, Little Endian. Umsetzung: `core/src/webpad.*`.

Client → Konsole:

| Typ | Name | Inhalt |
|---|---|---|
| `0x01` | HELLO | Version `u8` (= 1), Token (16 Byte), Profil-ID (`0xFF` = Gast) |
| `0x02` | STATE | Buttons `u16` (Bits wie `pd::Button`), lx, ly, rx, ry (`i8`, y > 0 unten) |
| `0x03` | PROFILE_CREATE | Name (8 Byte, nullgefüllt), Farbindex `u8` |
| `0x04` | PROFILE_SELECT | Profil-ID |
| `0x05` | PING | Folgenummer `u16`, alle 500 ms |

Konsole → Client:

| Typ | Name | Inhalt |
|---|---|---|
| `0x81` | WELCOME | Version `u8`, Anzahl Farben `u8`, Palette als RGB565 `u16` je Farbe |
| `0x82` | SLOT | Slot (`0xFF` = nicht beigetreten), Profil-ID (im Slot bzw. gewünscht) |
| `0x83` | RUMBLE | Dauer ms `u16` |
| `0x84` | PROFILES | Anzahl `u8`, je Profil: ID, Farbindex, Name (8 Byte) |
| `0x85` | PROFILE_SET | Profil-ID, die das Handy übernehmen soll (Systemmenü, neu angelegt) |
| `0x86` | RESYNC | bitte HELLO senden (Nachricht ohne vorheriges HELLO) |
| `0x88` | PONG | Folgenummer aus PING zurück |
| `0x87` | REJECT | Grund: 1 Konsole voll, 2 falsche Version, 3 anderswo verbunden, 4 Profil nicht angelegt |

- Nach HELLO schickt die Konsole WELCOME, PROFILES und SLOT; PROFILES und SLOT danach bei jeder Änderung.
- Geräte-ID = FNV-1a über das Token, Bits 31/30 = 01 (kollidiert nicht mit Tastaturen und SDL-Gamepads).
- Höchstens 8 Verbindungen gleichzeitig im Core; ist die Tabelle voll, wird eine Verbindung verdrängt, die seit über 1 s schweigt, sonst REJECT 1.

- STATE wird bei jeder Änderung und zusätzlich alle **100 ms** als Heartbeat gesendet. Kein Ereignisstrom, damit keine "hängenden" Tasten.
- Kein Heartbeat für **1 s** = getrennt (Slot bleibt reserviert, siehe 6.3).
- Latenz **[Annahme]** 10-50 ms; für Party-Spiele ausreichend, für präzise Geschicklichkeitsspiele grenzwertig. Nicht gemessen.
- Sticks schickt die Seite gebündelt alle 16 ms, Tasten sofort.
- PING/PONG (ergänzt nach dem ersten Handy-Test): Das Handy zeigt die Antwortzeit unter PROFIL an und "HAKT x s" in Rot, wenn 1,5 s keine Antwort kam. Vorher merkte das Handy Aussetzer gar nicht, nur die Konsole.
- Diagnose (vorläufig): Core loggt Lücken über 250 ms, Timeouts und ersetzte Verbindungen; der ESP loggt je WebSocket Lücken direkt beim Empfang, Schließen, langsames Senden und alle 5 s Frames, größte Lücke, Queue-Füllstand, RSSI und Heap-Minimum.

---

## 14. ESP32-Laufzeit

### 14.1 Tasks

| Kern | Aufgaben |
|---|---|
| 0 | WLAN-Stack, HTTP/WebSocket, UART-Empfang (`pd_link`, Prio 6), Audio-Mixer |
| 1 | OS-Hauptschleife `pd_main` (Prio 5): Snapshot → `tick()` → `present()` |

HUB75-DMA läuft ohne CPU-Last. Datenaustausch zwischen den Kernen nur über kleine gesperrte Puffer (Input-Zustand, Audio-Queue).

Stack der Hauptschleife: `PD_MAIN_STACK_SIZE`, Startwert **16 KB**. Große Arrays gehören in die Arena, nicht auf den Stack.

Umgesetzt (M3): `app_main` (Kern 0) initialisiert NVS, wertet den Neustartgrund aus, sammelt
Entropie (`bootloader_random`, vor dem I2S-Start), startet Display und UART-Task und legt dann
`pd_main` auf Kern 1 an. `CONFIG_FREERTOS_HZ = 1000`. `sleepUntil` wartet per `vTaskDelay` und
die letzte Millisekunde aktiv.

Messwerte M3 (Launcher und alle vier Spiele, Dev-Build):

| Wert | gemessen |
|---|---|
| Heap frei vor Display-Init | 216 KB, größter Block 108 KB |
| HUB75-DMA (Double Buffer, 8 Bit) | 32 KB Puffer + 2x 1024 Deskriptoren |
| Heap frei im Betrieb (Minimum) | ca. 134 KB |
| Stack `pd_main` belegt (Höchststand) | ca. 2,6 KB von 16 KB |
| `present()` (2048 Pixel + Flip) | 2,6 ms |
| `tick()` der Spiele, Schnitt / Maximum | 50-150 µs / 440 µs |
| statisches DRAM | 96 KB von 177 KB (davon Arena 64 KB) |

### 14.2 Hänger und Abstürze

- **Task-Watchdog** auf der Hauptschleife, Timeout **5 s** → Reset. Nur `pd_main` ist angemeldet (`hal::app::heartbeat()` je Durchlauf), die Idle-Tasks nicht.
- **Panic** (Nullzeiger, Stack-Überlauf) → IDF-Panic-Reset, Backtrace über UART.
- Vor bzw. bei beidem werden Grund und Spiel-ID im RTC-Speicher abgelegt. Nach dem Neustart zeigt das OS "SNAKE HAT NICHT REAGIERT" bzw. "SNAKE ABGESTÜRZT" und landet im Launcher.
- **Kein automatischer Neustart** des Spiels (Boot-Schleife).
- Umgesetzt (M3): Spiel-ID per `hal::app::setActivity` in `RTC_NOINIT`. Neustartgrund aus
  `esp_reset_reason()`: Task-/Interrupt-WDT = "HAT NICHT REAGIERT", Panic = "ABGESTUERZT"
  (die Fonts haben keine Umlaute). Ohne Spiel heißt es "SYSTEM". Der Hinweis steht 5 s oder bis
  A/B/Start, dann Launcher. Am Gerät getestet mit den Dev-Befehlen `hang` und `crash`.
- Letzte Rettung bleibt die Power-Taste am zweiten µC.

### 14.3 Shutdown

1. `shutdownPending` wird gesetzt.
2. OS ruft `onShutdown()` des laufenden Spiels, sichert OS-Daten, zeigt kurz "BYE".
3. OS ruft `hal::power::shutdownReady()`.
4. **[Offen]** Maximale Wartezeit des zweiten µC, falls `shutdownReady` nie kommt.

Stand M3: Der Core führt die Schritte 1-3 aus, sobald `shutdownPending` gesetzt ist (Spiel-
`onShutdown`, Einstellungen sichern, "BYE"). Bleibt das Abschalten aus und ist `shutdownPending`
3 s lang wieder false, geht es zurück in den Launcher. Es gibt keinen Rückkanal zum xmega;
`shutdownReady()` ist auf dem ESP nur ein Log-Eintrag. Statt Handshake hat der xmega eine feste
Gnadenfrist von 1,5 s (Abschnitt 1.2), damit ist Punkt 4 erledigt.

---

## 15. PC-Simulator

- SDL3-Fenster, Matrix hochskaliert.
- **LED-Optik:** runde Punkte mit dunklem Raster, umschaltbar auf harte Pixel.
- **Direktstart:** `pixeldeck --game snake --seed 42` überspringt den Launcher.
- **Tick-Budget-Warnung:** misst `tick()`, warnt bei Überschreitung eines Grenzwerts (`--tick-warn-us`, Standard **[Annahme]** 1666 µs = ca. 1/10 des Budgets, 0 = aus). **[Annahme]** Der PC ist grob 10-50x schneller als der ESP32; die Warnung ist nur ein Grobfilter, maßgeblich ist die Messung auf dem Gerät (Info-Menü).
- **Arena-Statistik:** Höchststand pro Spiel, Warnung über 80 %.
- **Screenshot und GIF-Aufnahme** per Taste, in Originalauflösung und hochskaliert. **[Offen]** GIF-Encoder auswählen, Lizenz prüfen.
- **Power simulieren:** Tasten für Akku leer, lädt, Power-Taste (Shutdown).
- **Zweites Tastaturlayout** als eigener Spieler (Pfeile vs. WASD), dazu SDL-Gamepads und Rumble. Umgesetzt (M4): Layout 1 liegt dafür auf der rechten Tastaturhälfte (Tabelle unten), Layout 2 links. Beide sind Geräte vom Typ `Keyboard` (ID 1 und 2); Layout 2 tritt wie ein Gamepad per A bei.
- **SDL-Gamepads (M4):** bis 6, Hotplug. Belegung nach Position wie am Nintendo-Pad **[Annahme]**: rechte Aktionstaste = A, untere = B, obere = X, linke = Y; Schultertasten und Trigger (> 50 %) = L/R, Back = Select. Sticks mit Totzone **[Annahme]** ca. 25 %. Geräte-ID aus GUID und Seriennummer (bei gleichen Pads ohne Seriennummer zusätzlich die Reihenfolge), damit derselbe Controller seinen Slot zurückbekommt. Rumble über `SDL_RumbleGamepad`. Im Headless- und Fuzz-Modus gibt es nur Tastatur 1.
- **Headless-Modus:** `--headless --ticks N --out frame.png`, für Smoke-Tests in CI. Ohne Fenster läuft eine virtuelle Uhr (so schnell wie möglich); die Tick-Messung ist dort ohne Aussage. `--fuzz-input <seed>` erzeugt zufällige Eingaben auf Slot 0; mit `--fuzz-players <n>` (1-4) kommen n-1 Zufalls-Gamepads dazu, die per A beitreten (ohne Start/Select). CTest startet jedes Spiel und den Launcher so, Snake zusätzlich mit 4 Spielern; ein Test scheitert bei `[ERROR]` oder einer Heap-Warnung.
- **WebPad (M6):** einschalten im Systemmenü oder mit `--webpad` (nur dieser Lauf). Das Handy öffnet `http://<PC-IP>:8080/` im selben Netz; Port per `--webpad-port`. `--webpad` funktioniert auch headless, dann mit echter Uhr statt virtueller (Ende-zu-Ende-Tests mit einem WebSocket-Client).
- **Heap-Hook:** meldet `operator new` während Spielcode läuft. Aufrufe des Spiels in OS-Dienste (Storage, Rumble) sind ausgenommen, weil dort allokiert werden darf.
- Hänger: Warnung im Log nach 5 s. Absturz: normaler Prozess-Crash mit Debugger.
- Nicht vorgesehen: Hot-Reload von Spielcode.

Tastaturbelegung (seit M4, vorher lagen die Aktionstasten von Layout 1 auf X Z S A):

| Funktion | Layout 1 (Primärgerät, rechts) | Layout 2 (links, Beitritt mit A) |
|---|---|---|
| D-Pad | Pfeiltasten | W A S D |
| A / B / X / Y | ; / . / O / K (Raute um L) | H / B / T / F (Raute um G) |
| L / R | I / P | Q / E |
| Start / Select | Enter / Rechts-Shift | 2 / 1 |
| linker Stick | Ziffernblock 8 4 5 6 | - |
| rechter Stick | Pos1 / Entf / Ende / Bild-ab | - |
| L3 / R3 | Ziffernblock 0 / Bild-auf | - |

Esc beendet den Simulator, F2 schaltet LED-Optik und Pixel um. Tasten gelten nach **physischer Position im US-Layout** (SDL-Scancodes). Auf deutschem QWERTZ liegt ";" auf "Ö".

---

## 16. Build, Repo, Assets, Tests

### 16.1 Werkzeuge

- **ESP32:** ESP-IDF **v5.5**, CMake. Projekt unter `platform/pixeldeck/esp32/`; Core und Spiele werden über dieselben CMake-Dateien eingebunden wie am PC. HUB75 über `ESP32-HUB75-MatrixPanel-DMA` **3.1.0** (MIT-Lizenz geprüft) per IDF Component Manager (Git-Tag, `dependencies.lock`).
- **Dev-Kanal (ESP32, `PD_DEV=ON`):** Textbefehle `!<befehl>` über USB → xmega → ESP-RX (`fb`, `press`, `stick`, `stat`, `raw`, `hang`, `crash`, `reset`), Werkzeug `tools/pd_serial.py`. Im Release (`-DPD_DEV=OFF`) nicht enthalten.
- **PC:** CMake, SDL3. Linux ist Pflicht, Windows optional.
- **Assets:** Python 3 + Pillow (Python ist durch ESP-IDF ohnehin vorhanden).
- **Tests:** doctest (MIT-Lizenz, header-only).
- **WebPad-Seite:** `tools/pd_embed.py` (Python 3, nur Standardbibliothek) bettet `web/webpad.html` gzip-komprimiert in `pd_core` ein; Ziel < 20 KB, der Build warnt darüber.
- **ESP32 und WLAN (M6):** `CONFIG_HTTPD_WS_SUPPORT=y`. Die main-Komponente wird mit `WHOLE_ARCHIVE` gelinkt, weil auf `hal_net.cpp` nur `pd_core` verweist und der Linker die Datei sonst verwirft.

### 16.2 Verzeichnisse

```
pixeldeck/
├── CMakeLists.txt            # PC-Build
├── core/
│   ├── include/pd/           # Spiel-API
│   │   └── hal/              # HAL-Schnittstelle (nur Header)
│   └── src/                  # OS
├── platform/
│   ├── pc/                   # SDL3-HAL, Simulator, main()
│   └── pixeldeck/            # Konsole
│       ├── esp32/            # IDF-Projekt, HAL, app_main()
│       └── xmega/            # Firmware Eingabe/Power-Controller (LUFA), kein Teil der HAL
├── games/<id>/               # game.cpp, assets/icon.png, ...
├── web/                      # WebPad-Seite
├── tools/                    # Asset-Konverter
├── docs/                     # diese Spezifikation
└── tests/                    # Unit-Tests (Test-HAL) + Headless-Smoke-Tests
```

- `core` und `games` linken weder SDL noch IDF. Falscher Include = Build-Fehler.
- Spiele werden automatisch aus `games/*` eingesammelt, Ausschluss per Konfigurationsliste.
- Unit-Tests linken gegen eine dritte, minimale **Test-HAL**.

### 16.3 Asset-Pipeline

- PNGs liegen beim Spiel (`games/<id>/assets/`).
- Der Konverter erzeugt beim Build `constexpr`-Arrays in RGB565 und einen Header `<id>_assets.h`. Generierte Dateien kommen nicht ins Repo.
- PNG-Alpha (< 50 % **[Annahme]**) wird zu `TRANSPARENT`.
- Spritesheets: Rastergröße im Dateinamen (`player_8x8.png` → `assets::player`, Frames zeilenweise). Begleitdatei noch nicht umgesetzt.
- `icon.png` (16x16) ist pro Spiel Pflicht; fehlt es, bricht CMake ab.
- Opakes reines Magenta wird mit Warnung zu `0xF81E`.
- Werkzeug: `tools/pd_assets.py` (Python 3 + Pillow). Der Header liegt unter `build/generated/<id>/`, Arrays haben interne Bindung, damit gleichnamige Assets verschiedener Spiele nicht kollidieren.

### 16.4 Konfigurationswerte

| Name | Startwert |
|---|---|
| `PD_ARENA_SIZE` | 32 KB (M6, vorher 64 KB) |
| `PD_SAVE_MAX` | 4 KB |
| `PD_MAIN_STACK_SIZE` | 16 KB |
| `PD_TICK_HZ` | 60 |
| `PD_WATCHDOG_MS` | 5000 |
| `PD_SLOT_HOLD_MS` | 10000 |
| `PD_AUDIO_RATE` | 22050 |
| `PD_MAX_PROFILES` | 16 |

---

## 17. Meilensteine

| | Inhalt |
|---|---|
| **M1 Gerüst (PC)** | CMake, Game-API, Arena, Gfx + beide Fonts, Input-Snapshot Slot 0 (Tastatur), SDL3-Fenster mit LED-Optik, Direktstart, Testspiel |
| **M2 Spiele + Launcher** | Launcher, Systemmenü (Weiter/Beenden/Helligkeit), Storage, Asset-Konverter, Headless-Smoke-Test, Snake, Pong, Fallblock-Spiel |
| **M3 ESP32** | IDF-Projekt, HAL für Display/Time/Storage/Log/Random, Input und Power über den xmega-Link, Watchdog und Crash-Meldung, Dev-Kanal |
| **M4 Mehrspieler + Profile** | Slot-Manager, Join, Lobby, Profile, Highscores, SDL-Gamepads, zweites Tastaturlayout |
| **M5 Audio** | Synth, Sfx-API, SDL-Ausgabe; ESP-Ausgabe erst mit Hardware |
| **M6 WebPad** | AP, Captive Portal, WebSocket-Protokoll, Controller-Seite, Rumble, auch am PC (umgesetzt vor M5) |

Startspiele (eigene Designs, keine Nachbildungen bestehender Spiele):
- **Snake** - 1-4P. Vor dem Start Modus wählen (Links/Rechts, wird im Spielstand gemerkt):
  - **KLASSIK**: nur Futter. Highscore nur allein im Klassik-Modus **[Annahme]**, damit Powerup- und Mehrspielerrunden die Liste nicht verzerren.
  - **POWER**: zusätzlich alle 3-7 s ein Powerup (max. 2 gleichzeitig, liegt 10 s, blinkt vor dem Verschwinden), +2 Punkte, wirkt nur auf den Fresser: blau = langsamer (6 s, Zugintervall x1,5), gelb = schneller (6 s, x2/3; hebt "langsamer" auf und umgekehrt), weiß = unverwundbar (5 s: durch alle Körper hindurch, Wände führen auf die Gegenseite; andere sterben weiterhin an diesem Körper), violett = unsichtbar (6 s, nur der Kopf ist zu sehen, der Körper bleibt Hindernis). Restzeit als Leiste unter der Punktezahl. Werte sind **[Annahme]**, nach Spieltest anpassen.
  - Ab 2 Spielern: feste Farben je Slot (grün, orange, cyan, pink), Startreihen abwechselnd von links und rechts, Countdown 2,5 s. Wand, Körper oder zwei Köpfe auf demselben Feld töten; Züge eines Ticks werden gemeinsam aufgelöst (Slot-Reihenfolge ohne Vorteil). Tote Körper bleiben 1 s als Hindernis. Letzter Überlebender gewinnt, Siege werden bis zum Moduswechsel gezählt; sterben alle gleichzeitig: unentschieden. Ab 3 Spielern liegen 2 Futter. Beitritt wirkt ab der nächsten Runde.
- **Tennis** (`tennis`) - 1-2P; ohne zweiten Spieler steuert die CPU die rechte Seite. Erster mit 7 Punkten gewinnt.
- **Fallblock-Spiel STAPEL** (`stapel`) - Zweierpaare farbiger Blöcke fallen in einen 6x10-Schacht; 3 oder mehr gleiche Farben waagerecht oder senkrecht lösen sich auf, Ketten zählen mehrfach. Highscore, Text, Spielstand (wird in `onShutdown` gesichert und beim nächsten Start fortgesetzt).
- später ein **4-Spieler-Partyspiel** mit Lichtspuren als Zugpferd für das WebPad

---

## 18. Offene Punkte

1. ~~UART-Protokoll zum zweiten µC~~ dokumentiert in 1.2, Paketrate und Gnadenfrist in M3 umgesetzt. Offen bleibt die Stick-Kalibrierung
2. RAM gemessen (14.1): genug Reserve; `PD_ARENA_SIZE` in M6 auf 32 KB halbiert (16), `PD_MAIN_STACK_SIZE` (16 KB) bleibt vorerst, Stack könnte auf 8 KB sinken. NVS-Partition 128 KB
3. Tick-Dauer gemessen (14.1): unter 0,5 ms. Die PC-Warnschwelle von 1666 µs bleibt
4. Audio-Hardware (I2S-DAC oder interner DAC)
5. ~~Fonts erstellen bzw. lizenzkonforme Quelle wählen~~ erledigt in M1 (selbst gepixelt)
6. ~~QR-Code-Lesbarkeit auf der Matrix testen~~ umgesetzt (12.2), mit Handykamera lesbar
7. ~~WLAN-Passwortschema für das WebPad~~ **[Annahme]** in M6: WPA2, 8 zufällige Ziffern pro Gerät (13.1); bestätigen
8. GIF-Encoder für den Simulator auswählen (Lizenz)
9. Registrierungs-Syntax, falls Windows/MSVC wichtig wird (Designated Initializers)
10. ~~Lizenz von `ESP32-HUB75-MatrixPanel-DMA` bestätigen~~ MIT, geprüft in M3
11. ~~Sichtprüfung am Gerät~~ in M3 bestätigt (Bild wie am PC, Gamma 2,2 passt); Tastenzuordnung und Stick-Richtungen ebenfalls
12. Profil anlegen direkt in der Profilwahl beim Join (bisher nur im Systemmenü)
13. Gamepad-Belegung nach Position (Nintendo) am echten Controller prüfen; Xbox-Nutzer erwarten "A" unten
14. ~~Am ESP sind Slots 1-3 bis zum WebPad (M6) nicht erreichbar~~ mit dem WebPad erreichbar
15. WebPad mit echten Handys prüfen (M6 nur mit WebSocket-Client am PC und AP-Sichtbarkeit am Gerät getestet): Captive Portal auf Android und iOS (öffnet sich die Seite, bleiben `localStorage` und WebSocket im Portal-Browser erhalten?), Vibration, Multitouch, Latenz, Heap mit 4 Handys. Test mit zwei Handys (M6, nach dem Socket-Fix): beide gleichzeitig verbunden und beigetreten, Captive Portal öffnete sich auf iOS (`hotspot-detect.html`) und Android (`generate_204`). Tokens bleiben pro Browser erhalten, aber Portal-Browser und normaler Browser desselben Handys haben getrennte Speicher, also zwei Geräte-IDs und getrennte Profilwahl. Aussetzer von 1-2 s mit zwei Handys: ein Teil kam von einem zweiten, im Hintergrund gedrosselten Browser desselben Handys (Portal-Fenster, 1 Nachricht/s), dazu Lücken von 300-600 ms bei knappem Heap (4,9 KB). Nach Socket-Fix, TCP_NODELAY und Puffer-Tuning im Test mit zwei Handys keine Aussetzer mehr bemerkt; Diagnose bleibt vorerst drin. iOS: das Captive-Portal-Fenster dreht nicht ins Querformat, in Safari geht es; die Seite zeigt hochkant einen Hinweis dazu
16. RAM mit WLAN knapp: mit zwei echten Handys fiel der Heap vor dem Puffer-Tuning auf 4,9 KB, mit 3 simulierten danach auf 11 KB. Umgesetzt: `PD_ARENA_SIZE` von 64 auf 32 KB (Spiele brauchen bisher max. 1,3 KB)
17. M5 Audio steht noch aus (M6 wurde vorgezogen); `Audio::play` ist weiter ein No-Op
