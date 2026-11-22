# Plattformunabhängiger Kern. Linkt weder SDL noch ESP-IDF.

# WebPad-Seite (SPEC 13.1) gzip-komprimiert einbetten
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(PD_REPO ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
set(PD_WEBPAD_PAGE ${CMAKE_CURRENT_BINARY_DIR}/generated/webpad_page.cpp)
add_custom_command(
  OUTPUT ${PD_WEBPAD_PAGE}
  COMMAND ${Python3_EXECUTABLE} ${PD_REPO}/tools/pd_embed.py --out ${PD_WEBPAD_PAGE}
          --symbol kWebPadPage ${PD_REPO}/web/webpad.html
  DEPENDS ${PD_REPO}/web/webpad.html ${PD_REPO}/tools/pd_embed.py
  COMMENT "WebPad-Seite einbetten"
  VERBATIM)

add_library(pd_core STATIC
  src/context.cpp
  src/font_data.cpp
  src/gfx.cpp
  src/input_system.cpp
  src/os_apps.cpp
  src/profiles.cpp
  src/qrcode.cpp
  src/runtime.cpp
  src/score_table.cpp
  src/settings.cpp
  src/storage.cpp
  src/stubs.cpp
  src/webpad.cpp
  ${PD_WEBPAD_PAGE}
)
target_include_directories(pd_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pd_core PUBLIC pd_config PRIVATE pd_flags)
