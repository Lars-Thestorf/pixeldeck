# Unit-Tests gegen eine minimale Test-HAL (SPEC 16.2). doctest: MIT-Lizenz, header-only.
set(DOCTEST_H ${CMAKE_BINARY_DIR}/_deps/doctest/doctest/doctest.h)
if(NOT EXISTS ${DOCTEST_H})
  message(STATUS "pixeldeck: lade doctest 2.4.11")
  file(DOWNLOAD
    https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
    ${DOCTEST_H} STATUS st TLS_VERIFY ON)
  list(GET st 0 code)
  if(NOT code EQUAL 0)
    file(REMOVE ${DOCTEST_H})
    message(FATAL_ERROR "doctest konnte nicht geladen werden: ${st}")
  endif()
endif()

add_executable(pd_tests
  main.cpp
  test_hal.cpp
  test_gfx.cpp
  test_arena.cpp
  test_input.cpp
  test_runtime.cpp
  test_game.cpp
  test_game_duo.cpp
  test_profiles.cpp
  test_registry.cpp
  test_webpad.cpp
)
set_source_files_properties(test_game.cpp PROPERTIES COMPILE_DEFINITIONS PD_GAME_SYMBOL=testgame)
set_source_files_properties(test_game_duo.cpp PROPERTIES COMPILE_DEFINITIONS PD_GAME_SYMBOL=duo)
target_include_directories(pd_tests PRIVATE
  ${CMAKE_BINARY_DIR}/_deps/doctest/doctest
  ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(pd_tests PRIVATE pd_core)
add_test(NAME pd_tests COMMAND pd_tests)

# Headless-Smoke-Tests (SPEC 15): jedes Spiel und der Launcher mit Zufallseingaben,
# ohne Fenster. Prüft Abstürze, Hänger und das PNG-Ergebnis.
set(smoke_dir ${CMAKE_BINARY_DIR}/smoke)
file(MAKE_DIRECTORY ${smoke_dir})
foreach(t IN LISTS PD_GAME_TARGETS)
  string(REPLACE "pd_game_" "" id ${t})
  add_test(NAME smoke_${id}
    COMMAND pixeldeck --headless --game ${id} --ticks 3600 --seed 1 --fuzz-input 7
            --data-dir ${smoke_dir}/data --out ${smoke_dir}/${id}.png)
  set_tests_properties(smoke_${id} PROPERTIES TIMEOUT 60
    FAIL_REGULAR_EXPRESSION "Heap-Allokation;\\[ERROR\\]")
endforeach()
# Snake mit 4 Zufallsspielern (Mehrspieler- und Powerup-Pfad)
add_test(NAME smoke_snake_4p
  COMMAND pixeldeck --headless --game snake --ticks 36000 --seed 3 --fuzz-input 11
          --fuzz-players 4 --data-dir ${smoke_dir}/data4 --out ${smoke_dir}/snake_4p.png)
set_tests_properties(smoke_snake_4p PROPERTIES TIMEOUT 60
  FAIL_REGULAR_EXPRESSION "Heap-Allokation;\\[ERROR\\]")
add_test(NAME smoke_launcher
  COMMAND pixeldeck --headless --ticks 20000 --seed 1 --fuzz-input 3
          --data-dir ${smoke_dir}/data --out ${smoke_dir}/launcher.png)
set_tests_properties(smoke_launcher PROPERTIES TIMEOUT 120
  FAIL_REGULAR_EXPRESSION "Heap-Allokation;\\[ERROR\\]")

# QR-Encoder gegen einen echten Decoder prüfen (nur wenn zbarimg installiert ist)
find_program(PD_ZBARIMG zbarimg)
add_executable(pd_qrcheck qrcheck.cpp)
target_include_directories(pd_qrcheck PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(pd_qrcheck PRIVATE pd_core)
if(PD_ZBARIMG)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/qr)
  add_test(NAME qr_zbar COMMAND pd_qrcheck ${PD_ZBARIMG} ${CMAKE_BINARY_DIR}/qr)
else()
  message(STATUS "pixeldeck: zbarimg fehlt, QR-Decoder-Test entfällt")
endif()
