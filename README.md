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
