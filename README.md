# pixeldeck

Mini-OS mit Spielen für eine ESP32-Konsole mit 64x32-LED-Matrix. Spezifikation: [docs/SPEC.md](docs/SPEC.md).

## PC-Simulator bauen (Linux)

Voraussetzungen: CMake ≥ 3.24, C++17-Compiler, Python 3 mit Pillow. SDL 3.2.10 wird per
FetchContent gebaut, falls nicht installiert.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build        # Unit-Tests + Headless-Smoke-Tests
./build/pixeldeck             # Launcher
./build/pixeldeck --game snake
```

Optionen: `./build/pixeldeck --help`. Esc beendet, F2 schaltet LED-Optik/Pixel.
Systemmenü: Start im Launcher, Enter+Rechts-Shift 1 s halten im Spiel.

Tastatur 1 (Spieler 1, rechte Hälfte): Pfeile, `;` `.` `O` `K` = A B X Y, `I`/`P` = L/R.
Tastatur 2 (linke Hälfte): WASD, `H` `B` `T` `F` = A B X Y, `Q`/`E` = L/R, `2`/`1` = Start/Select.
Tastatur 2 und SDL-Gamepads treten mit A bei; Profile, Spieler und Rekorde im Systemmenü.

## CI und Releases

GitHub Actions ([.github/workflows/pc-build.yml](.github/workflows/pc-build.yml)) baut und testet
den PC-Simulator für Linux und Windows (MinGW, statisch gelinkt) bei jedem Push auf `main` und
bei Pull Requests. Die Binaries hängen als Artefakte am Lauf. Ein Tag `v*` legt zusätzlich ein
GitHub-Release mit beiden Binaries an:

```sh
git tag v0.2.0 && git push origin v0.2.0
```

## WebPad (Handy als Controller)

Systemmenü > WEBPAD > A schaltet es ein (bleibt gespeichert).

- **Konsole:** Das Handy verbindet sich mit dem WLAN `PIXELDECK-XXXX` (Passwort steht auf der
  WEBPAD-Seite); die Controller-Seite öffnet sich als Captive Portal, sonst `http://192.168.77.1/`.
- **PC:** `./build/pixeldeck --webpad` (oder im Menü einschalten), am Handy im selben Netz
  `http://<PC-IP>:8080/` öffnen. Port per `--webpad-port`.

Am Handy Profil wählen oder anlegen, dann A drücken zum Mitspielen. Quelle der Seite:
`web/webpad.html`, eingebettet beim Build (`tools/pd_embed.py`).

## ESP32 (Konsole)

Voraussetzungen: ESP-IDF **v5.5** (`IDF_PATH`, `. $IDF_PATH/export.sh`), System-Python 3 mit
Pillow für den Asset-Konverter (wird automatisch gesucht, alternativ `-DPD_ASSET_PYTHON=...`).
Die HUB75-Lib (ESP32-HUB75-MatrixPanel-DMA 3.1.0, MIT) holt der IDF Component Manager.

```sh
cd platform/pixeldeck/esp32
idf.py set-target esp32       # einmalig
idf.py build
idf.py -p /dev/ttyACM0 flash  # flasht über die xmega-USB-Brücke
idf.py -DPD_DEV=OFF build     # Release ohne Dev-Kanal
```

Hinweise:
- Liegt die Rust-ESP-Toolchain (`~/.rustup/toolchains/esp/...`) im `PATH`, bricht `export.sh`
  von IDF 5.5 ab. Vorher aus dem `PATH` nehmen.
- Jedes Öffnen von `/dev/ttyACM0` startet den ESP neu (Linux setzt DTR/RTS, der xmega zieht EN).
  Für Log und Dev-Kanal den Port daher mit `tools/pd_serial.py serve` dauerhaft offen halten;
  vor `idf.py flash` beenden.

## xmega (Eingabe- und Power-Controller)

Firmware unter `platform/pixeldeck/xmega` (ATxmega128A4U, LUFA 170418 als Teilmenge unter
`lufa/LUFA`, Lizenz dort). Übernommen aus dem HMatrix-Projekt, in M3 um festen 200-Hz-Takt und
Power-Taste mit Gnadenfrist erweitert (SPEC 1.2).

```sh
cd platform/pixeldeck/xmega
make                                          # braucht gcc-avr, avr-libc
# DFU: Gerät aus, einen Joystick gedrückt halten, einschalten (USB dran)
dfu-programmer atxmega128a4u erase
dfu-programmer atxmega128a4u flash LaLeMa.hex
dfu-programmer atxmega128a4u start
```

Dev-Kanal (nur mit `PD_DEV=ON`, Standard), mit System-Python (pyserial, Pillow):

```sh
tools/pd_serial.py serve &                    # Log: ~/.cache/pixeldeck/serial.log
tools/pd_serial.py cmd stat                   # Heap, Stack, xmega-Paketrate, Rohwerte
tools/pd_serial.py cmd "press start,select 1500"
tools/pd_serial.py fb bild.png                # aktuelles Bild als PNG
tools/pd_serial.py cmd "raw on"               # xmega-Rohwerte bei Änderung loggen
```

Weitere Befehle: `stick <lx> <ly> <rx> <ry> [ms]`, `hang` (Watchdog-Test), `crash`, `reset`.

## Spiele

| id | Inhalt |
|---|---|
| `snake` | Schlange, 1-4P; Modus KLASSIK (allein mit Highscore) oder POWER (Powerups: langsamer, schneller, unverwundbar, unsichtbar); ab 2P gewinnt, wer übrig bleibt |
| `stapel` | Fallblock-Spiel mit Farbreihen, 1P, Spielstand |
| `tennis` | Schläger und Ball, 1-2P (CPU als Gegner, zweiter Spieler per Join) |
| `testbild` | Testseiten für Eingabe, Fonts, Grafik, Sprites |

## Neues Spiel

Ordner `games/<id>/` mit mindestens einer `.cpp`-Datei und `assets/icon.png` (16x16) anlegen.
Der Ordnername ist die Spiel-id (`[a-z0-9_]`, max. 12 Zeichen) und muss mit `.id` in
`PD_REGISTER_GAME` übereinstimmen. PNGs unter `assets/` stehen als `assets::<name>` im
Header `<id>_assets.h` bereit. CMake sammelt das Spiel beim nächsten Build automatisch ein.
