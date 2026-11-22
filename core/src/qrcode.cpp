#include "qrcode.h"

#include <cstdlib>
#include <cstring>

namespace pd::os {

namespace {

// Fehlerkorrektur L, Version 1-4: Datenkodewörter und EC-Kodewörter (je ein Block)
constexpr int kDataCw[QrCode::MAX_VERSION + 1] = {0, 19, 34, 55, 80};
constexpr int kEcCw[QrCode::MAX_VERSION + 1] = {0, 7, 10, 15, 20};

constexpr char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

int alnumIndex(char c) {
  for (int i = 0; i < 45; ++i)
    if (kAlnum[i] == c) return i;
  return -1;
}

uint8_t gfMul(uint8_t a, uint8_t b) {  // GF(256) mit Polynom 0x11D
  unsigned r = 0;
  for (int i = 7; i >= 0; --i) {
    r = (r << 1) ^ ((r >> 7) * 0x11D);
    if ((b >> i) & 1) r ^= a;
  }
  return uint8_t(r);
}

// Reed-Solomon: Generatorpolynom (Grad n, ohne führende 1) und Rest der Division
void rsEcc(const uint8_t* data, int n, int ecLen, uint8_t* ec) {
  uint8_t gen[32] = {};
  gen[ecLen - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < ecLen; ++i) {
    for (int j = 0; j < ecLen; ++j) {
      gen[j] = gfMul(gen[j], root);
      if (j + 1 < ecLen) gen[j] ^= gen[j + 1];
    }
    root = gfMul(root, 2);
  }
  std::memset(ec, 0, size_t(ecLen));
  for (int i = 0; i < n; ++i) {
    const uint8_t f = data[i] ^ ec[0];
    std::memmove(ec, ec + 1, size_t(ecLen - 1));
    ec[ecLen - 1] = 0;
    for (int j = 0; j < ecLen; ++j) ec[j] ^= gfMul(gen[j], f);
  }
}

struct Bits {
  uint8_t* buf;
  int cap;   // Bytes
  int n = 0; // Bits
  void put(unsigned v, int len) {
    for (int i = len - 1; i >= 0; --i) {
      if ((v >> i) & 1) buf[n >> 3] |= uint8_t(0x80 >> (n & 7));
      ++n;
    }
  }
};

}  // namespace

void QrCode::setFunc(int x, int y, bool d) { m_[y][x] = uint8_t(kFunc | (d ? kDark : 0)); }

bool QrCode::encode(const char* text) {
  size_ = version_ = 0;
  const int len = int(std::strlen(text));
  bool alnum = true;
  for (int i = 0; i < len; ++i)
    if (alnumIndex(text[i]) < 0) alnum = false;
  // Bits: Modus 4 + Zeichenzahl (V1-9: alnum 9, byte 8) + Daten
  const int bits = 4 + (alnum ? 9 + (len / 2) * 11 + (len % 2) * 6 : 8 + len * 8);
  int v = 1;
  while (v <= MAX_VERSION && bits > kDataCw[v] * 8) ++v;
  if (v > MAX_VERSION || len > 255) return false;
  version_ = v;
  size_ = 17 + 4 * v;

  uint8_t cw[100] = {};
  const int dataCw = kDataCw[v];
  Bits b{cw, dataCw};
  if (alnum) {
    b.put(0x2, 4);
    b.put(unsigned(len), 9);
    for (int i = 0; i + 1 < len; i += 2) b.put(unsigned(alnumIndex(text[i]) * 45 + alnumIndex(text[i + 1])), 11);
    if (len % 2) b.put(unsigned(alnumIndex(text[len - 1])), 6);
  } else {
    b.put(0x4, 4);
    b.put(unsigned(len), 8);
    for (int i = 0; i < len; ++i) b.put(uint8_t(text[i]), 8);
  }
  const int capBits = dataCw * 8;
  b.put(0, capBits - b.n < 4 ? capBits - b.n : 4);  // Terminator
  if (b.n % 8) b.put(0, 8 - b.n % 8);
  for (uint8_t pad = 0xEC; b.n < capBits; pad ^= 0xEC ^ 0x11) b.put(pad, 8);
  rsEcc(cw, dataCw, kEcCw[v], cw + dataCw);

  std::memset(m_, 0, sizeof m_);
  drawFunctionPatterns();
  drawCodewords(cw, dataCw + kEcCw[v]);
  // Maske mit der geringsten Strafpunktzahl wählen
  long best = -1;
  for (int k = 0; k < 8; ++k) {
    applyMask(k);
    drawFormatBits(k);
    const long p = penalty();
    if (best < 0 || p < best) { best = p; mask_ = k; }
    applyMask(k);  // XOR: wieder zurück
  }
  applyMask(mask_);
  drawFormatBits(mask_);
  return true;
}

void QrCode::drawFunctionPatterns() {
  const int s = size_;
  for (int i = 0; i < s; ++i) {  // Taktlinien
    setFunc(6, i, i % 2 == 0);
    setFunc(i, 6, i % 2 == 0);
  }
  const int fc[3][2] = {{3, 3}, {s - 4, 3}, {3, s - 4}};  // Suchmuster mit Trennrand
  for (const auto& c : fc)
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int x = c[0] + dx, y = c[1] + dy;
        if (x < 0 || y < 0 || x >= s || y >= s) continue;
        const int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        setFunc(x, y, d != 2 && d != 4);
      }
  if (version_ >= 2) {  // ein Ausrichtungsmuster unten rechts (V2-6)
    const int p = s - 7;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        const int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        setFunc(p + dx, p + dy, d != 1);
      }
  }
  drawFormatBits(0);  // reserviert die Formatfelder
}

void QrCode::drawFormatBits(int mask) {
  const int s = size_;
  const unsigned data = (1u << 3) | unsigned(mask);  // Fehlerkorrektur L = 01
  unsigned rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  const unsigned bits = ((data << 10) | rem) ^ 0x5412;
  auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
  for (int i = 0; i <= 5; ++i) setFunc(8, i, bit(i));
  setFunc(8, 7, bit(6));
  setFunc(8, 8, bit(7));
  setFunc(7, 8, bit(8));
  for (int i = 9; i < 15; ++i) setFunc(14 - i, 8, bit(i));
  for (int i = 0; i < 8; ++i) setFunc(s - 1 - i, 8, bit(i));
  for (int i = 8; i < 15; ++i) setFunc(8, s - 15 + i, bit(i));
  setFunc(8, s - 8, true);  // dunkles Modul
}

void QrCode::drawCodewords(const uint8_t* cw, int n) {
  const int s = size_;
  int i = 0;
  for (int right = s - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < s; ++vert)
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool up = ((right + 1) & 2) == 0;
        const int y = up ? s - 1 - vert : vert;
        if (m_[y][x] & kFunc) continue;
        if (i < n * 8 && ((cw[i >> 3] >> (7 - (i & 7))) & 1)) m_[y][x] |= kDark;
        ++i;  // Restbits bleiben hell
      }
  }
}

void QrCode::applyMask(int mask) {
  for (int y = 0; y < size_; ++y)
    for (int x = 0; x < size_; ++x) {
      if (m_[y][x] & kFunc) continue;
      bool inv = false;
      switch (mask) {
        case 0: inv = (x + y) % 2 == 0; break;
        case 1: inv = y % 2 == 0; break;
        case 2: inv = x % 3 == 0; break;
        case 3: inv = (x + y) % 3 == 0; break;
        case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
        case 5: inv = x * y % 2 + x * y % 3 == 0; break;
        case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
        default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
      }
      if (inv) m_[y][x] ^= kDark;
    }
}

// Strafpunkte nach ISO/IEC 18004, 7.8.3 (N1 Läufe, N2 Blöcke, N3 Suchmuster-ähnlich, N4 Balance)
long QrCode::penalty() const {
  long p = 0;
  auto at = [this](int x, int y, bool col) { return col ? dark(y, x) : dark(x, y); };
  for (int pass = 0; pass < 2; ++pass) {
    const bool col = pass == 1;
    for (int y = 0; y < size_; ++y) {
      int run = 1;
      for (int x = 1; x <= size_; ++x) {
        if (x < size_ && at(x, y, col) == at(x - 1, y, col)) { ++run; continue; }
        if (run >= 5) p += 3 + (run - 5);
        run = 1;
      }
      // 1:1:3:1:1 mit 4 hellen Modulen davor oder danach (außerhalb = hell)
      for (int x = -4; x < size_; ++x) {
        static const bool pat[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
        bool a = true, b = true;
        for (int k = 0; k < 11; ++k) {
          const int xx = x + k;
          const bool d = xx >= 0 && xx < size_ && at(xx, y, col);
          if (d != pat[k]) a = false;
          if (d != pat[10 - k]) b = false;
        }
        if (a) p += 40;
        if (b) p += 40;
      }
    }
  }
  int darkCount = 0;
  for (int y = 0; y < size_; ++y)
    for (int x = 0; x < size_; ++x) {
      if (dark(x, y)) ++darkCount;
      if (x + 1 < size_ && y + 1 < size_) {
        const bool d = dark(x, y);
        if (dark(x + 1, y) == d && dark(x, y + 1) == d && dark(x + 1, y + 1) == d) p += 3;
      }
    }
  const int total = size_ * size_;
  int k = 0;
  while (std::abs(darkCount * 20 - total * 10) > (k + 1) * total) ++k;
  p += 10L * k;
  return p;
}

}  // namespace pd::os
