#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "config.h"
#include "game.h"
#include "sprite.h"

namespace pd {

enum class ScoreMode : uint8_t { None, Higher, Lower };

// Metadaten (SPEC 4.2). Reihenfolge der Felder = Reihenfolge der Designated Initializers.
struct GameMeta {
  const char* id;                         // stabil, [a-z0-9_], 1-12 Zeichen, = Ordnername
  const char* name;                       // Anzeige im Launcher
  Sprite      icon;                       // 16x16
  ScoreMode   scoreMode = ScoreMode::None;
  uint8_t     minPlayers = 1;
  uint8_t     maxPlayers = 1;
  uint16_t    saveVersion = 1;
  bool        stickAsDpad = false;        // linken Stick zusätzlich als D-Pad melden
};

struct GameInfo {
  GameMeta meta;
  size_t   size, align;
  Game*  (*create)(void* mem);            // Placement-new in mem
};

namespace detail {
constexpr size_t MAX_ID_LEN = 12;  // NVS: 15 Zeichen, 3 für OS-Präfixe reserviert

constexpr bool validId(const char* s) {
  size_t n = 0;
  for (; s[n]; ++n) {
    char c = s[n];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return n >= 1 && n <= MAX_ID_LEN;
}

constexpr bool streq(const char* a, const char* b) {
  size_t i = 0;
  for (; a[i] && b[i]; ++i)
    if (a[i] != b[i]) return false;
  return a[i] == b[i];
}
}  // namespace detail

}  // namespace pd

#define PD_DETAIL_CAT2(a, b) a##b
#define PD_DETAIL_CAT(a, b) PD_DETAIL_CAT2(a, b)
#define PD_DETAIL_STR2(x) #x
#define PD_DETAIL_STR(x) PD_DETAIL_STR2(x)

// Registriert ein Spiel. Nur im globalen Namensraum verwenden, einmal pro Spielordner.
// PD_GAME_SYMBOL (= Ordnername) setzt CMake; daraus entsteht die Funktion
// pd_game_entry_<ordner>(), die die generierte game_registry.cpp referenziert.
#define PD_REGISTER_GAME(Cls, ...)                                                    \
  namespace {                                                                         \
  constexpr ::pd::GameMeta pd_game_meta_ = __VA_ARGS__;                               \
  static_assert(::pd::detail::validId(pd_game_meta_.id),                              \
                "Spiel-id: nur [a-z0-9_], 1-12 Zeichen");                             \
  static_assert(::pd::detail::streq(pd_game_meta_.id, PD_DETAIL_STR(PD_GAME_SYMBOL)), \
                "Spiel-id muss dem Ordnernamen unter games/ entsprechen");            \
  static_assert(std::is_base_of<::pd::Game, Cls>::value, "Spiel muss von pd::Game erben"); \
  static_assert(sizeof(Cls) <= PD_ARENA_SIZE, "Spielobjekt größer als PD_ARENA_SIZE"); \
  static_assert(pd_game_meta_.minPlayers >= 1 &&                                      \
                    pd_game_meta_.minPlayers <= pd_game_meta_.maxPlayers &&           \
                    pd_game_meta_.maxPlayers <= 4,                                    \
                "Spielerzahl: 1 <= minPlayers <= maxPlayers <= 4");                   \
  const ::pd::GameInfo pd_game_info_{pd_game_meta_, sizeof(Cls), alignof(Cls),        \
                                     [](void* m) -> ::pd::Game* { return new (m) Cls(); }}; \
  }                                                                                   \
  const ::pd::GameInfo& PD_DETAIL_CAT(pd_game_entry_, PD_GAME_SYMBOL)() { return pd_game_info_; }
