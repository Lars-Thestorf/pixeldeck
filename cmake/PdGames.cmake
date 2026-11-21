# Sammelt alle Spiele unter games/<id>/ ein und erzeugt game_registry.cpp (SPEC 4.2).
#
# Jeder Ordner mit mindestens einer .cpp-Datei ist ein Spiel. Der Ordnername ist
# die Spiel-id ([a-z0-9_], 1-12 Zeichen) und muss mit .id in PD_REGISTER_GAME
# übereinstimmen (per static_assert geprüft). Ergebnis:
#   PD_GAME_TARGETS  - Liste der Spiel-Bibliotheken
#   PD_GAME_REGISTRY - Pfad der generierten game_registry.cpp
function(pd_add_game id dir)
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${dir}/*.cpp)
  file(GLOB pngs CONFIGURE_DEPENDS ${dir}/assets/*.png)
  if(NOT EXISTS ${dir}/assets/icon.png)
    message(FATAL_ERROR "Spiel '${id}': games/${id}/assets/icon.png (16x16) fehlt")
  endif()
  # Asset-Header <id>_assets.h (SPEC 16.3), wird nicht ins Repo eingecheckt
  set(gen_dir ${CMAKE_BINARY_DIR}/generated/${id})
  set(hdr ${gen_dir}/${id}_assets.h)
  add_custom_command(
    OUTPUT ${hdr}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/pd_assets.py --out ${hdr} ${pngs}
    DEPENDS ${pngs} ${PROJECT_SOURCE_DIR}/tools/pd_assets.py
    COMMENT "Assets ${id}"
    VERBATIM)
  add_library(pd_game_${id} STATIC ${srcs} ${hdr})
  target_include_directories(pd_game_${id} PRIVATE ${dir} ${gen_dir})
  target_link_libraries(pd_game_${id} PRIVATE pd_core pd_flags)
  target_compile_definitions(pd_game_${id} PRIVATE PD_GAME_SYMBOL=${id})
endfunction()

function(pd_collect_games games_dir)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # CONFIGURE_DEPENDS: neuer Spielordner löst beim nächsten Build ein Re-Configure aus
  file(GLOB game_cpps CONFIGURE_DEPENDS ${games_dir}/*/*.cpp)
  set(ids "")
  foreach(f IN LISTS game_cpps)
    get_filename_component(d ${f} DIRECTORY)
    get_filename_component(id ${d} NAME)
    list(APPEND ids ${id})
  endforeach()
  list(REMOVE_DUPLICATES ids)
  list(SORT ids)

  set(decls "")
  set(entries "")
  set(targets "")
  foreach(id IN LISTS ids)
    if(id IN_LIST PD_GAMES_EXCLUDE)
      message(STATUS "pixeldeck: Spiel '${id}' ausgeschlossen")
      continue()
    endif()
    if(NOT id MATCHES "^[a-z0-9_]+$")
      message(FATAL_ERROR "Spielordner '${id}': nur [a-z0-9_] erlaubt")
    endif()
    string(LENGTH "${id}" len)
    if(len GREATER 12)
      message(FATAL_ERROR "Spielordner '${id}': max. 12 Zeichen")
    endif()
    pd_add_game(${id} ${games_dir}/${id})
    list(APPEND targets pd_game_${id})
    string(APPEND decls "const pd::GameInfo& pd_game_entry_${id}();\n")
    string(APPEND entries "    &pd_game_entry_${id},\n")
  endforeach()
  list(LENGTH targets count)

  if(count EQUAL 0)
    set(entries "    nullptr,\n")
  endif()
  set(content "// Generiert von cmake/PdGames.cmake - nicht bearbeiten.\n#include <cstddef>\n#include \"pd/registry.h\"\n\n${decls}\nnamespace pd::os::detail {\nusing GameEntry = const GameInfo& (*)();\nextern const GameEntry kGames[];\nextern const size_t kGameCount;\nconst GameEntry kGames[] = {\n${entries}};\nconst size_t kGameCount = ${count};\n}  // namespace pd::os::detail\n")

  set(out ${CMAKE_BINARY_DIR}/generated/game_registry.cpp)
  file(WRITE ${out}.tmp "${content}")
  configure_file(${out}.tmp ${out} COPYONLY)  # nur bei Änderung neu schreiben
  message(STATUS "pixeldeck: ${count} Spiel(e): ${targets}")

  set(PD_GAME_TARGETS ${targets} PARENT_SCOPE)
  set(PD_GAME_REGISTRY ${out} PARENT_SCOPE)
endfunction()
