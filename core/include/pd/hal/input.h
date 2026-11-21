#pragma once
#include <cstddef>
#include <cstdint>

namespace pd::hal::input {

enum class DeviceType : uint8_t { Builtin, Keyboard, Gamepad };

struct RawInput {
  uint16_t buttons;          // Bitmaske wie pd::Button
  int8_t lx, ly, rx, ry;     // kalibriert, mit Deadzone, y > 0 unten
};

struct Device {
  uint32_t   id;             // stabil über Neuverbindungen
  DeviceType type;
  char       name[16];
  bool       connected;
  RawInput   state;
};

size_t poll(Device* out, size_t max);     // alle bekannten Hardware-Geräte
void   rumble(uint32_t id, uint16_t ms);  // darf No-Op sein

}  // namespace pd::hal::input
