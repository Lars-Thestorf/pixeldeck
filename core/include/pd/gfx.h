#pragma once
#include <cstdint>

#include "color.h"
#include "font.h"
#include "sprite.h"

namespace pd {

// Zeichnet in den Framebuffer des OS. Alle Funktionen clippen (SPEC 5.3).
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

  // Zeichnet Text, '\n' beginnt eine neue Zeile. Gibt die Breite der
  // breitesten Zeile zurück. Zeichen außerhalb ASCII 32-126 werden als '?'
  // gezeichnet. Font::Small kennt keine Kleinbuchstaben und zeigt Großbuchstaben.
  int  text(int x, int y, const char* str, uint16_t c, Font f = Font::Small);
  int  textWidth(const char* str, Font f = Font::Small) const;

  uint16_t*       buffer() { return fb_; }  // direkter Zugriff, Zeilen hintereinander
  const uint16_t* buffer() const { return fb_; }

private:
  void glyph(int x, int y, char ch, uint16_t c, Font f);
  uint16_t fb_[W * H] = {};
};

}  // namespace pd
