// Minimaler PNG-Schreiber (unkomprimierte Deflate-Blöcke), ohne Fremdbibliothek.
#include <cstdio>
#include <vector>

#include "sim.h"

namespace pd::pc {
namespace {

uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t v = i;
      for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      table[i] = v;
    }
    init = true;
  }
  c = ~c;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(uint8_t(v >> 24)); o.push_back(uint8_t(v >> 16));
  o.push_back(uint8_t(v >> 8));  o.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  be32(o, uint32_t(data.size()));
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  o.insert(o.end(), td.begin(), td.end());
  be32(o, crc32(td.data(), td.size()));
}

}  // namespace

bool writePng(const char* path, const uint16_t* fb, int w, int h) {
  // Rohdaten: pro Zeile Filterbyte 0 + RGB
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    for (int x = 0; x < w; ++x) {
      uint16_t c = fb[y * w + x];
      raw.push_back(uint8_t(((c >> 11) & 31) * 255 / 31));
      raw.push_back(uint8_t(((c >> 5) & 63) * 255 / 63));
      raw.push_back(uint8_t((c & 31) * 255 / 31));
    }
  }
  // zlib-Strom mit "stored"-Blöcken
  std::vector<uint8_t> z{0x78, 0x01};
  size_t pos = 0;
  do {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
    z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
    pos += n;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);

  std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  be32(ihdr, uint32_t(w)); be32(ihdr, uint32_t(h));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 Bit, RGB
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});

  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace pd::pc
