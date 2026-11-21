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

## ESP32 (Konsole)

Voraussetzungen: ESP-IDF **v5.5** (`IDF_PATH`, `. $IDF_PATH/export.sh`), System-Python 3 mit
Pillow für den Asset-Konverter (wird automatisch gesucht, alternativ `-DPD_ASSET_PYTHON=...`).
Die HUB75-Lib (ESP32-HUB75-MatrixPanel-DMA 3.1.0, MIT) holt der IDF Component Manager.

```sh
cd platform/esp32
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
| `snake` | Schlange, 1P, Highscore |
| `stapel` | Fallblock-Spiel mit Farbreihen, 1P, Spielstand |
| `tennis` | Schläger und Ball, 1-2P (CPU als Gegner) |
| `testbild` | Testseiten für Eingabe, Fonts, Grafik, Sprites |

## Neues Spiel

Ordner `games/<id>/` mit mindestens einer `.cpp`-Datei und `assets/icon.png` (16x16) anlegen.
Der Ordnername ist die Spiel-id (`[a-z0-9_]`, max. 12 Zeichen) und muss mit `.id` in
`PD_REGISTER_GAME` übereinstimmen. PNGs unter `assets/` stehen als `assets::<name>` im
Header `<id>_assets.h` bereit. CMake sammelt das Spiel beim nächsten Build automatisch ein.
