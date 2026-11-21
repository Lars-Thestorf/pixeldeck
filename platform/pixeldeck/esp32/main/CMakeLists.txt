# ESP32-Plattform: HAL + app_main. Core und Spiele kommen als normale CMake-Targets
# aus dem Repo dazu (dieselben CMake-Dateien wie beim PC-Build).
idf_component_register(
  SRCS app_main.cpp hal_display.cpp hal_link.cpp hal_system.cpp
  INCLUDE_DIRS .
  REQUIRES hub75 nvs_flash esp_timer esp_driver_uart esp_driver_gpio bootloader_support
           esp_system)

get_filename_component(PD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../.. ABSOLUTE)

option(PD_DEV "Dev-Kanal über UART (Befehle !fb, !press, !stat, ...)" ON)
set(PD_ARENA_SIZE 65536 CACHE STRING "Arena-Größe in Bytes (SPEC 9)")
set(PD_GAMES_EXCLUDE "" CACHE STRING "Spiel-Ordner, die nicht gebaut werden (;-Liste)")
set(PD_PANEL_GAMMA 2.2 CACHE STRING "Gamma der Panel-Korrektur (Display-HAL)")

add_library(pd_config INTERFACE)
target_compile_definitions(pd_config INTERFACE PD_ARENA_SIZE=${PD_ARENA_SIZE})

add_library(pd_flags INTERFACE)
target_compile_options(pd_flags INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -fno-exceptions -fno-rtti>
  -Wall -Wextra -Wshadow -Wno-missing-field-initializers)

add_subdirectory(${PD_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/pd_core)
include(${PD_ROOT}/cmake/PdGames.cmake)
pd_collect_games(${PD_ROOT}/games)

# Die Registry gehört zu pd_core (sie wird von dort referenziert); libmain ist hier selbst
# eine statische Bibliothek und käme in der Link-Reihenfolge zu früh.
target_sources(pd_core PRIVATE ${PD_GAME_REGISTRY})
target_link_libraries(pd_core PRIVATE ${PD_GAME_TARGETS})
target_link_libraries(${COMPONENT_LIB} PRIVATE pd_core pd_flags)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  PD_DEV=$<BOOL:${PD_DEV}> PD_PANEL_GAMMA=${PD_PANEL_GAMMA}f)
