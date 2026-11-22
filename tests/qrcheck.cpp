// Prüft den QR-Encoder gegen einen echten Decoder: kodiert Texte, schreibt PGM-Bilder
// (4 Module Rand, 8 px pro Modul) und lässt sie von zbarimg lesen.
// Aufruf: pd_qrcheck <zbarimg> <ausgabeverzeichnis>
#include <cstdio>
#include <cstring>
#include <string>

#include "qrcode.h"

namespace {
bool writePgm(const pd::os::QrCode& q, const std::string& path) {
  const int border = 4, scale = 8, n = (q.size() + 2 * border) * scale;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P5\n%d %d\n255\n", n, n);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const int mx = x / scale - border, my = y / scale - border;
      const bool d = mx >= 0 && my >= 0 && mx < q.size() && my < q.size() && q.dark(mx, my);
      std::fputc(d ? 0 : 255, f);
    }
  std::fclose(f);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const char* cases[] = {
      "WIFI:T:WPA;S:PIXELDECK-C7C1;P:89671780;;",  // Byte, Version 3
      "WIFI:T:WPA;S:PIXELDECK-0A3F;P:00000000;;",
      "HTTP://192.168.77.1",                      // alphanumerisch, Version 1
      "HTTP://192.168.77.1/",
      "A",
      "pixeldeck",                                // Byte, Version 1
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:0123456789ABCDEFGHIJKLMNOPQRSTUVW",  // alnum V4
      "Kleinbuchstaben und Satzzeichen; passt noch in Version 4!!!!!!!!!!!!!",
  };
  int fails = 0, i = 0;
  for (const char* text : cases) {
    pd::os::QrCode q;
    if (!q.encode(text)) { std::printf("FEHLER: passt nicht: %s\n", text); ++fails; continue; }
    const std::string path = std::string(argv[2]) + "/qr" + std::to_string(i++) + ".pgm";
    writePgm(q, path);
    const std::string cmd = std::string(argv[1]) + " --raw -q " + path + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    char out[256] = {};
    const size_t got = p ? std::fread(out, 1, sizeof out - 1, p) : 0;
    if (p) pclose(p);
    out[got] = 0;
    if (got && out[got - 1] == '\n') out[got - 1] = 0;
    const bool ok = std::strcmp(out, text) == 0;
    std::printf("%s V%d (%dx%d) Maske %d: %s\n", ok ? "ok    " : "FEHLER", q.version(), q.size(),
                q.size(), q.mask(), text);
    if (!ok) { std::printf("       gelesen: '%s'\n", out); ++fails; }
  }
  pd::os::QrCode q;
  if (q.encode("Dieser Text ist viel zu lang fuer Version 4 mit Fehlerkorrektur L, er hat mehr als 78 Zeichen")) {
    std::printf("FEHLER: zu langer Text wurde angenommen\n");
    ++fails;
  }
  return fails ? 1 : 0;
}
