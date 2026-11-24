#pragma once
#include "pd/lang.h"
#include "pd/registry.h"

namespace pd::os {

// Aktuelle Sprache der Oberfläche; gesetzt aus den Einstellungen (Runtime::setLang).
extern Lang g_lang;

inline const char* tr(const char* en, const char* de) { return g_lang == Lang::De ? de : en; }
inline const char* gameName(const GameMeta& m) {
  return g_lang == Lang::De && m.nameDe ? m.nameDe : m.name;
}

}  // namespace pd::os
