#include <doctest.h>

#include "pd/gfx.h"

using namespace pd;

namespace {
int countColor(const Gfx& g, uint16_t c) {
  int n = 0;
  for (int i = 0; i < Gfx::W * Gfx::H; ++i) n += g.buffer()[i] == c;
  return n;
}
}  // namespace

TEST_CASE("rgb565") {
  CHECK(rgb(255, 255, 255) == 0xFFFF);
  CHECK(rgb(255, 0, 0) == 0xF800);
  CHECK(rgb(0, 255, 0) == 0x07E0);
  CHECK(rgb(0, 0, 255) == 0x001F);
  CHECK(rgb(255, 0, 255) == TRANSPARENT);
}

TEST_CASE("pixel und get clippen") {
  static Gfx g;
  g.clear(0);
  g.pixel(-1, 0, 1);
  g.pixel(64, 0, 1);
  g.pixel(0, 32, 1);
  g.pixel(0, -5, 1);
  CHECK(countColor(g, 1) == 0);
  g.pixel(63, 31, 5);
  CHECK(g.get(63, 31) == 5);
  CHECK(g.get(64, 31) == 0);
  CHECK(g.get(-1, -1) == 0);
}

TEST_CASE("Linien und Rechtecke clippen") {
  static Gfx g;
  g.clear(0);
  g.hline(-10, 3, 20, 1);
  CHECK(countColor(g, 1) == 10);
  g.clear(0);
  g.vline(5, -100, 1000, 1);
  CHECK(countColor(g, 1) == 32);
  g.clear(0);
  g.fillRect(60, 30, 10, 10, 2);
  CHECK(countColor(g, 2) == 4 * 2);
  g.clear(0);
  g.fillRect(0, 0, -3, 5, 2);
  CHECK(countColor(g, 2) == 0);
  g.clear(0);
  g.rect(0, 0, 4, 3, 3);
  CHECK(countColor(g, 3) == 10);
  g.clear(0);
  g.line(-1000, -1000, 1000, 1000, 4);  // Diagonale durch den Bildschirm
  CHECK(countColor(g, 4) == 32);
  CHECK(g.get(0, 0) == 4);
  CHECK(g.get(31, 31) == 4);
}

TEST_CASE("Kreise") {
  static Gfx g;
  g.clear(0);
  g.fillCircle(32, 16, 0, 1);
  CHECK(countColor(g, 1) == 1);
  g.clear(0);
  g.fillCircle(32, 16, 5, 1);
  int filled = countColor(g, 1);
  CHECK(filled > 70);   // pi*25 ~ 78
  CHECK(filled < 100);
  g.circle(32, 16, 5, 2);
  CHECK(g.get(37, 16) == 2);
  CHECK(g.get(32, 11) == 2);
  g.clear(0);
  g.circle(0, 0, 40, 3);  // fast komplett außerhalb, darf nicht abstürzen
  CHECK(countColor(g, 3) > 0);
}

TEST_CASE("blit mit Transparenz und Spiegeln") {
  static const uint16_t d[] = {1, TRANSPARENT, 3, 4};  // 2x2
  const Sprite s{2, 2, 1, d};
  static Gfx g;
  g.clear(9);
  g.blit(s, 0, 0);
  CHECK(g.get(0, 0) == 1);
  CHECK(g.get(1, 0) == 9);  // transparent
  CHECK(g.get(0, 1) == 3);
  CHECK(g.get(1, 1) == 4);
  g.clear(9);
  g.blit(s, 0, 0, FLIP_X);
  CHECK(g.get(0, 0) == 9);
  CHECK(g.get(1, 0) == 1);
  g.clear(9);
  g.blit(s, 0, 0, FLIP_X | FLIP_Y);
  CHECK(g.get(0, 0) == 4);
  CHECK(g.get(1, 1) == 1);
  CHECK(g.get(0, 1) == 9);  // transparent
  g.clear(9);
  g.blit(s, -1, -1);  // nur rechts unten sichtbar
  CHECK(g.get(0, 0) == 4);
}

TEST_CASE("Sprite-Frames") {
  static const uint16_t d[] = {1, 2, 3};  // 1x1, 3 Frames
  const Sprite s{1, 1, 3, d};
  CHECK(s.frame(0).data[0] == 1);
  CHECK(s.frame(2).data[0] == 3);
  CHECK(s.frame(4).data[0] == 2);  // modulo
}

TEST_CASE("Text: Breite und Zeichnen") {
  static Gfx g;
  CHECK(g.textWidth("") == 0);
  CHECK(g.textWidth("A") == 3);
  CHECK(g.textWidth("AB") == 7);
  CHECK(g.textWidth("AB", Font::Large) == 11);
  CHECK(g.textWidth("ABC\nA") == 11);
  CHECK(g.textWidth("0123456789ABCDEF") == 63);           // 16 Zeichen passen
  CHECK(g.textWidth("0123456789", Font::Large) == 59);    // 10 Zeichen passen

  g.clear(0);
  CHECK(g.text(0, 0, "I", 1) == 3);
  // Small 'I' = "### .#. .#. .#. ###" -> 9 Pixel
  CHECK(countColor(g, 1) == 9);
  CHECK(g.get(0, 0) == 1);
  CHECK(g.get(0, 1) == 0);
  CHECK(g.get(1, 1) == 1);

  g.clear(0);
  g.text(0, 0, " ", 1);
  CHECK(countColor(g, 1) == 0);

  g.clear(0);
  g.text(62, 30, "WWW", 1, Font::Large);  // ragt hinaus, nur geclippt
  CHECK(countColor(g, 1) > 0);

  // Small kennt keine Kleinbuchstaben: 'a' == 'A'
  static Gfx g2;
  g.clear(0);
  g2.clear(0);
  g.text(0, 0, "abc", 1);
  g2.text(0, 0, "ABC", 1);
  bool same = true;
  for (int i = 0; i < Gfx::W * Gfx::H; ++i) same &= g.buffer()[i] == g2.buffer()[i];
  CHECK(same);
}
