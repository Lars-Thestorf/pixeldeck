// PC-Simulator: Einstieg (SPEC 15)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pd/os/runtime.h"
#include "sim.h"

namespace {

void usage() {
  std::printf(
      "pixeldeck - PC-Simulator\n\n"
      "  --game <id>         Spiel direkt starten (ohne: Launcher)\n"
      "  --seed <n>          PRNG-Seed festlegen (reproduzierbare Abläufe)\n"
      "  --scale <n>         Fenstergröße in Bildschirmpixeln pro LED (Standard 12)\n"
      "  --pixels            harte Pixel statt LED-Optik (umschaltbar mit F2)\n"
      "  --ticks <n>         nach n Ticks beenden\n"
      "  --tick-warn-us <n>  Warnung, wenn tick() länger dauert (Standard 1666, 0 = aus)\n"
      "  --headless          ohne Fenster, virtuelle Uhr (braucht --ticks)\n"
      "  --out <datei.png>   letztes Bild am Ende als PNG speichern (64x32)\n"
      "  --fuzz-input <seed> zufällige Eingaben auf Slot 0 (Smoke-Tests)\n"
      "  --data-dir <pfad>   Ablage für Spielstände/Einstellungen (Standard ~/.pixeldeck)\n"
      "  --list              registrierte Spiele auflisten\n"
      "  --help              diese Hilfe\n\n"
      "Tastatur 1 (Slot 0, rechte Hälfte): Pfeile D-Pad, ; . O K = A B X Y, I/P = L/R,\n"
      "  Enter/RShift = Start/Select, Ziffernblock 8456 linker Stick,\n"
      "  Pos1/Entf/Ende/Bild-ab rechter Stick, KP0/Bild-auf = L3/R3.\n"
      "Tastatur 2 (linke Hälfte, Beitritt mit A): WASD D-Pad, H B T F = A B X Y, Q/E = L/R,\n"
      "  2/1 = Start/Select.\n"
      "Gamepads (SDL): Beitritt mit A (rechte Aktionstaste), Rumble wird unterstützt.\n"
      "Tasten nach US-Position. Esc beendet, F2 LED-Optik/Pixel.\n"
      "Systemmenü: Start im Launcher, Start+Select 1 s im Spiel.\n");
}

bool parseU32(const char* s, uint32_t& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  unsigned long v = std::strtoul(s, &end, 0);
  if (*end != 0 || v > 0xFFFFFFFFul) return false;
  out = uint32_t(v);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  pd::os::RunOptions opts;
  const char* out = nullptr;
  opts.tickWarnUs = 1666;  // [Annahme] ca. 1/10 des Tick-Budgets, siehe SPEC 15

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
    uint32_t n = 0;
    if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
      usage();
      return 0;
    } else if (!std::strcmp(a, "--list")) {
      for (size_t k = 0; k < pd::os::gameCount(); ++k) {
        const pd::GameMeta& m = pd::os::game(k).meta;
        std::printf("%-12s  %-12s  %u-%uP\n", m.id, m.name, unsigned(m.minPlayers),
                    unsigned(m.maxPlayers));
      }
      return 0;
    } else if (!std::strcmp(a, "--game") && v) {
      opts.gameId = v; ++i;
    } else if (!std::strcmp(a, "--seed") && parseU32(v, n)) {
      opts.fixedSeed = true; opts.seed = n; ++i;
    } else if (!std::strcmp(a, "--scale") && parseU32(v, n) && n >= 1 && n <= 64) {
      pd::pc::config().scale = int(n); ++i;
    } else if (!std::strcmp(a, "--pixels")) {
      pd::pc::config().led = false;
    } else if (!std::strcmp(a, "--ticks") && parseU32(v, n)) {
      opts.maxTicks = n; ++i;
    } else if (!std::strcmp(a, "--headless")) {
      pd::pc::config().headless = true;
    } else if (!std::strcmp(a, "--out") && v) {
      out = v; ++i;
    } else if (!std::strcmp(a, "--fuzz-input") && parseU32(v, n)) {
      pd::pc::config().fuzzInput = true; pd::pc::config().fuzzSeed = n; ++i;
    } else if (!std::strcmp(a, "--data-dir") && v) {
      pd::pc::config().dataDir = v; ++i;
    } else if (!std::strcmp(a, "--tick-warn-us") && parseU32(v, n)) {
      opts.tickWarnUs = n; ++i;
    } else {
      std::fprintf(stderr, "Unbekannte oder unvollständige Option: %s\n\n", a);
      usage();
      return 1;
    }
  }

  if (pd::pc::config().headless && opts.maxTicks == 0) {
    std::fprintf(stderr, "--headless braucht --ticks <n>\n");
    return 1;
  }

  int rc = pd::os::run(opts);
  if (rc == 0 && out && !pd::pc::writeLastFrame(out)) {
    std::fprintf(stderr, "[ERROR] PNG konnte nicht geschrieben werden: %s\n", out);
    rc = 4;
  }
  pd::pc::shutdown();
  return rc;
}
