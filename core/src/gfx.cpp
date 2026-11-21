#include "pd/gfx.h"

#include <cstdlib>

#include "font_data.h"

namespace pd {

namespace {
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void Gfx::clear(uint16_t c) {
  for (int i = 0; i < W * H; ++i) fb_[i] = c;
}

void Gfx::pixel(int x, int y, uint16_t c) {
  if (unsigned(x) < unsigned(W) && unsigned(y) < unsigned(H)) fb_[y * W + x] = c;
}

uint16_t Gfx::get(int x, int y) const {
  return (unsigned(x) < unsigned(W) && unsigned(y) < unsigned(H)) ? fb_[y * W + x] : 0;
}

void Gfx::hline(int x, int y, int w, uint16_t c) {
  if (w <= 0 || unsigned(y) >= unsigned(H)) return;
  int x0 = clampi(x, 0, W), x1 = clampi(x + w, 0, W);
  for (uint16_t* p = fb_ + y * W + x0; x0 < x1; ++x0) *p++ = c;
}

void Gfx::vline(int x, int y, int h, uint16_t c) {
  if (h <= 0 || unsigned(x) >= unsigned(W)) return;
  int y0 = clampi(y, 0, H), y1 = clampi(y + h, 0, H);
  for (; y0 < y1; ++y0) fb_[y0 * W + x] = c;
}

void Gfx::line(int x0, int y0, int x1, int y1, uint16_t c) {
  // Bresenham; Clipping pro Pixel genügt bei 64x32. Sehr weit außerhalb
  // liegende Endpunkte werden vorher grob begrenzt, damit die Schleife kurz bleibt.
  const int lim = 4096;
  x0 = clampi(x0, -lim, lim); y0 = clampi(y0, -lim, lim);
  x1 = clampi(x1, -lim, lim); y1 = clampi(y1, -lim, lim);
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Gfx::rect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, c);
  if (h > 1) hline(x, y + h - 1, w, c);
  if (h > 2) {
    vline(x, y + 1, h - 2, c);
    if (w > 1) vline(x + w - 1, y + 1, h - 2, c);
  }
}

void Gfx::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  int y0 = clampi(y, 0, H), y1 = clampi(y + h, 0, H);
  for (; y0 < y1; ++y0) hline(x, y0, w, c);
}

void Gfx::circle(int cx, int cy, int r, uint16_t c) {
  if (r < 0) return;
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    pixel(cx + x, cy + y, c); pixel(cx - x, cy + y, c);
    pixel(cx + x, cy - y, c); pixel(cx - x, cy - y, c);
    pixel(cx + y, cy + x, c); pixel(cx - y, cy + x, c);
    pixel(cx + y, cy - x, c); pixel(cx - y, cy - x, c);
    ++y;
    if (err < 0) err += 2 * y + 1;
    else { --x; err += 2 * (y - x) + 1; }
  }
}

void Gfx::fillCircle(int cx, int cy, int r, uint16_t c) {
  if (r < 0) return;
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    hline(cx - x, cy + y, 2 * x + 1, c);
    hline(cx - x, cy - y, 2 * x + 1, c);
    hline(cx - y, cy + x, 2 * y + 1, c);
    hline(cx - y, cy - x, 2 * y + 1, c);
    ++y;
    if (err < 0) err += 2 * y + 1;
    else { --x; err += 2 * (y - x) + 1; }
  }
}

void Gfx::blit(const Sprite& s, int x, int y, uint8_t flags) {
  if (!s.data || s.w == 0 || s.h == 0) return;
  const bool fx = flags & FLIP_X, fy = flags & FLIP_Y;
  int sx0 = clampi(-x, 0, s.w), sx1 = clampi(W - x, 0, s.w);
  int sy0 = clampi(-y, 0, s.h), sy1 = clampi(H - y, 0, s.h);
  for (int sy = sy0; sy < sy1; ++sy) {
    const uint16_t* row = s.data + (fy ? s.h - 1 - sy : sy) * s.w;
    uint16_t* dst = fb_ + (y + sy) * W + x;
    for (int sx = sx0; sx < sx1; ++sx) {
      uint16_t c = row[fx ? s.w - 1 - sx : sx];
      if (c != TRANSPARENT) dst[sx] = c;
    }
  }
}

void Gfx::glyph(int x, int y, char ch, uint16_t c, Font f) {
  if (ch < 32 || ch > 126) ch = '?';
  if (f == Font::Small) {
    uint16_t bits = os::kFontSmall[size_t(ch - 32)];
    for (int r = 0; r < 5; ++r)
      for (int col = 0; col < 3; ++col)
        if (bits & (1u << (14 - (r * 3 + col)))) pixel(x + col, y + r, c);
  } else {
    uint64_t bits = os::kFontLarge[size_t(ch - 32)];
    for (int r = 0; r < 7; ++r)
      for (int col = 0; col < 5; ++col)
        if (bits & (uint64_t(1) << (34 - (r * 5 + col)))) pixel(x + col, y + r, c);
  }
}

int Gfx::text(int x, int y, const char* str, uint16_t c, Font f) {
  if (!str) return 0;
  const FontMetrics m = fontMetrics(f);
  int cx = x, cy = y;
  for (const char* p = str; *p; ++p) {
    if (*p == '\n') { cx = x; cy += m.lineHeight; continue; }
    // ganz außerhalb liegende Glyphen überspringen
    if (cx < W && cy < H && cx + m.glyphW > 0 && cy + m.glyphH > 0) glyph(cx, cy, *p, c, f);
    cx += m.advance;
  }
  return textWidth(str, f);
}

int Gfx::textWidth(const char* str, Font f) const {
  if (!str) return 0;
  const FontMetrics m = fontMetrics(f);
  int best = 0, n = 0;
  for (const char* p = str;; ++p) {
    if (*p == '\n' || *p == 0) {
      int w = n > 0 ? n * m.advance - 1 : 0;  // kein Abstand nach dem letzten Zeichen
      if (w > best) best = w;
      n = 0;
      if (*p == 0) break;
    } else {
      ++n;
    }
  }
  return best;
}

}  // namespace pd
