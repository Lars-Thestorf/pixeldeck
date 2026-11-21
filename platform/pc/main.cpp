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
      "  --game <id>         Spiel direkt starten (ohne: erstes Spiel)\n"
      "  --seed <n>          PRNG-Seed festlegen (reproduzierbare Abläufe)\n"
      "  --scale <n>         Fenstergröße in Bildschirmpixeln pro LED (Standard 12)\n"
      "  --pixels            harte Pixel statt LED-Optik (umschaltbar mit F2)\n"
      "  --ticks <n>         nach n Ticks beenden\n"
      "  --tick-warn-us <n>  Warnung, wenn tick() länger dauert (Standard 1666, 0 = aus)\n"
      "  --list              registrierte Spiele auflisten\n"
      "  --help              diese Hilfe\n\n"
      "Tasten (Slot 0): Pfeile D-Pad, X/Z/S/A = A/B/X/Y, Q/W = L/R, Enter/RShift =\n"
      "Start/Select, IJKL linker Stick, Ziffernblock 8456 rechter Stick, U/KP0 = L3/R3.\n"
      "Esc beendet, F2 LED-Optik/Pixel, Start+Select 1 s beendet das Spiel (Menü ab M2).\n");
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
    } else if (!std::strcmp(a, "--tick-warn-us") && parseU32(v, n)) {
      opts.tickWarnUs = n; ++i;
    } else {
      std::fprintf(stderr, "Unbekannte oder unvollständige Option: %s\n\n", a);
      usage();
      return 1;
    }
  }

  const int rc = pd::os::run(opts);
  pd::pc::shutdown();
  return rc;
}
