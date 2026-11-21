# pixeldeck

Mini-OS mit Spielen für eine ESP32-Konsole mit 64x32-LED-Matrix. Spezifikation: [docs/SPEC.md](docs/SPEC.md).

## PC-Simulator bauen (Linux)

```sh
cmake -S . -B build          # holt SDL 3.2.10 per FetchContent, falls nicht installiert
cmake --build build -j
ctest --test-dir build       # Unit-Tests
./build/pixeldeck --game testbild
```

Optionen: `./build/pixeldeck --help`. Esc beendet, F2 schaltet LED-Optik/Pixel.

## Neues Spiel

Ordner `games/<id>/` mit mindestens einer `.cpp`-Datei anlegen. Der Ordnername ist die
Spiel-id (`[a-z0-9_]`, max. 12 Zeichen) und muss mit `.id` in `PD_REGISTER_GAME`
übereinstimmen. CMake sammelt das Spiel beim nächsten Build automatisch ein.
Beispiel: [games/testbild/game.cpp](games/testbild/game.cpp).
