#pragma once
#include <cstdint>
#include <string>

// Einstellungen des PC-Simulators, von main() vor pd::os::run() gesetzt.
namespace pd::pc {

struct SimConfig {
  int  scale = 12;          // Bildschirmpixel pro LED beim Fensterstart
  bool led = true;          // LED-Optik (runde Punkte) statt harter Pixel
  bool headless = false;    // kein Fenster, virtuelle Uhr (läuft so schnell wie möglich)
  bool fuzzInput = false;   // zufällige Eingaben auf Slot 0 (Smoke-Tests)
  uint32_t fuzzSeed = 1;
  std::string dataDir;      // Ablage für Storage; leer = Standardpfad
};

SimConfig& config();
void shutdown();                                 // SDL aufräumen
bool writeLastFrame(const char* path);           // letztes Bild als PNG (64x32)
bool writePng(const char* path, const uint16_t* fb, int w, int h);

}  // namespace pd::pc
