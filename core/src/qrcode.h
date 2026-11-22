#pragma once
#include <cstdint>

namespace pd::os {

// Kleiner QR-Code-Encoder für die WebPad-Anzeige (SPEC 12.2, 13.1). Eigene Umsetzung nach
// ISO/IEC 18004, beschränkt auf das, was auf 32 Pixel Höhe passt: Version 1-4, Fehlerkorrektur
// L (bei diesen Versionen genau ein Reed-Solomon-Block), Modus alphanumerisch oder Byte.
// Keine Heap-Allokation, ca. 1,2 KB Zustand.
class QrCode {
public:
  static constexpr int MAX_VERSION = 4;
  static constexpr int MAX_SIZE = 17 + 4 * MAX_VERSION;  // 33 Module

  // Kodiert text (ASCII). Alphanumerisch, wenn alle Zeichen dazu passen (0-9 A-Z Leerzeichen
  // $ % * + - . / :), sonst Byte-Modus. false, wenn der Text nicht in Version 4 passt.
  bool encode(const char* text);

  int  size() const { return size_; }        // Module pro Seite, 0 = nichts kodiert
  int  version() const { return version_; }
  int  mask() const { return mask_; }
  bool dark(int x, int y) const { return (m_[y][x] & kDark) != 0; }

private:
  static constexpr uint8_t kDark = 1, kFunc = 2;
  void setFunc(int x, int y, bool d);
  void drawFunctionPatterns();
  void drawFormatBits(int mask);
  void drawCodewords(const uint8_t* cw, int n);
  void applyMask(int mask);
  long penalty() const;

  uint8_t m_[MAX_SIZE][MAX_SIZE] = {};
  int size_ = 0, version_ = 0, mask_ = 0;
};

}  // namespace pd::os
