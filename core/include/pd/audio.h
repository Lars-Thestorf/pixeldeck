#pragma once
#include <cstdint>

namespace pd {

// Skizze nach SPEC 10. Synth und Ausgabe folgen in M5, bis dahin No-Op.
enum class Wave : uint8_t { Square, Triangle, Noise };

struct Sfx {
  Wave     wave = Wave::Square;
  uint16_t freqStart = 440, freqEnd = 440;  // Hz, linearer Sweep
  uint16_t durationMs = 100;
  uint8_t  volume = 200;                    // 0-255
  uint8_t  duty = 128;                      // nur Square
  uint8_t  attackMs = 0, releaseMs = 0;
};

class Audio {
public:
  void play(const Sfx& sfx, int channel = -1);  // -1: OS wählt passenden Kanal
  void stop(int channel);
};

}  // namespace pd
