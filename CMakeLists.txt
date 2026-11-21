cmake_minimum_required(VERSION 3.24)
project(pixeldeck VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(PD_BUILD_TESTS "Unit-Tests bauen" ON)
option(PD_FETCH_SDL3  "SDL3 per FetchContent holen, falls nicht installiert" ON)
set(PD_ARENA_SIZE 65536 CACHE STRING "Arena-Größe in Bytes (SPEC 9)")
set(PD_GAMES_EXCLUDE "" CACHE STRING "Spiel-Ordner, die nicht gebaut werden (;-Liste)")

# Konfigurationswerte für Core und Spiele
add_library(pd_config INTERFACE)
target_compile_definitions(pd_config INTERFACE PD_ARENA_SIZE=${PD_ARENA_SIZE})

# Sprachregeln (SPEC 3): keine Exceptions, kein RTTI
add_library(pd_flags INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(pd_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
    -Wall -Wextra -Wshadow -Wno-missing-field-initializers)
elseif(MSVC)
  target_compile_options(pd_flags INTERFACE /GR- /W4)
endif()

add_subdirectory(core)

include(cmake/PdGames.cmake)
pd_collect_games(${PROJECT_SOURCE_DIR}/games)

add_subdirectory(platform/pc)

if(PD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
