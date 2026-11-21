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
  test_registry.cpp
)
set_source_files_properties(test_game.cpp PROPERTIES COMPILE_DEFINITIONS PD_GAME_SYMBOL=testgame)
target_include_directories(pd_tests PRIVATE
  ${CMAKE_BINARY_DIR}/_deps/doctest/doctest
  ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(pd_tests PRIVATE pd_core)
add_test(NAME pd_tests COMMAND pd_tests)
