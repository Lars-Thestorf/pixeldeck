# Sammelt alle Spiele unter games/<id>/ ein und erzeugt game_registry.cpp (SPEC 4.2).
#
# Jeder Ordner mit mindestens einer .cpp-Datei ist ein Spiel. Der Ordnername ist
# die Spiel-id ([a-z0-9_], 1-12 Zeichen) und muss mit .id in PD_REGISTER_GAME
# übereinstimmen (per static_assert geprüft). Ergebnis:
#   PD_GAME_TARGETS  - Liste der Spiel-Bibliotheken
#   PD_GAME_REGISTRY - Pfad der generierten game_registry.cpp
#
# Wird vom PC-Build und vom IDF-Projekt (platform/pixeldeck/esp32) eingebunden.

get_filename_component(PD_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

# Python mit Pillow für den Asset-Konverter. Das IDF-venv hat kein Pillow; dann wird
# ein System-Python gesucht. Vorgabe per -DPD_ASSET_PYTHON=<pfad> möglich.
function(pd_find_asset_python)
  if(PD_ASSET_PYTHON)
    return()
  endif()
  find_package(Python3 QUIET COMPONENTS Interpreter)
  set(candidates ${Python3_EXECUTABLE})
  find_program(sys_py NAMES python3 PATHS /usr/bin /usr/local/bin /opt/homebrew/bin NO_DEFAULT_PATH)
  list(APPEND candidates ${sys_py})
  find_program(path_py NAMES python3 python)
  list(APPEND candidates ${path_py})
  foreach(py IN LISTS candidates)
    execute_process(COMMAND ${py} -c "import PIL" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(rc EQUAL 0)
      set(PD_ASSET_PYTHON ${py} CACHE FILEPATH "Python 3 mit Pillow für tools/pd_assets.py")
      message(STATUS "pixeldeck: Asset-Konverter mit ${py}")
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "Kein Python 3 mit Pillow gefunden (pip install pillow) "
                      "oder -DPD_ASSET_PYTHON=<pfad> setzen")
endfunction()
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
    COMMAND ${PD_ASSET_PYTHON} ${PD_ROOT}/tools/pd_assets.py --out ${hdr} ${pngs}
    DEPENDS ${pngs} ${PD_ROOT}/tools/pd_assets.py
    COMMENT "Assets ${id}"
    VERBATIM)
  add_library(pd_game_${id} STATIC ${srcs} ${hdr})
  target_include_directories(pd_game_${id} PRIVATE ${dir} ${gen_dir})
  target_link_libraries(pd_game_${id} PRIVATE pd_core pd_flags)
  target_compile_definitions(pd_game_${id} PRIVATE PD_GAME_SYMBOL=${id})
endfunction()

function(pd_collect_games games_dir)
  pd_find_asset_python()
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
