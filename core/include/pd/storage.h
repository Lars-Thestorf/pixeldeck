#pragma once
#include <cstddef>
#include <cstdint>

namespace pd {

namespace os { class Runtime; }

// Ein Spielstand-Blob pro Spiel im eigenen Namensraum (SPEC 8.1).
class Storage {
public:
  // Lädt den Spielstand. false, wenn nicht vorhanden, Größe abweicht
  // oder saveVersion nicht passt.
  bool load(void* buf, size_t len);
  bool save(const void* buf, size_t len);   // max. PD_SAVE_MAX
  void erase();

private:
  friend class os::Runtime;
  char     ns_[16] = {};                    // "g_" + Spiel-id; leer = ungebunden
  uint16_t version_ = 0;
};

}  // namespace pd
