#pragma once

// Einstellungen des PC-Simulators, von main() vor pd::os::run() gesetzt.
namespace pd::pc {

struct SimConfig {
  int  scale = 12;     // Bildschirmpixel pro LED beim Fensterstart
  bool led = true;     // LED-Optik (runde Punkte) statt harter Pixel
};

SimConfig& config();
void shutdown();       // SDL aufräumen

}  // namespace pd::pc
