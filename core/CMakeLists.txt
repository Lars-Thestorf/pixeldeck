# Plattformunabhängiger Kern. Linkt weder SDL noch ESP-IDF.
add_library(pd_core STATIC
  src/context.cpp
  src/font_data.cpp
  src/gfx.cpp
  src/input_system.cpp
  src/runtime.cpp
  src/stubs.cpp
)
target_include_directories(pd_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pd_core PUBLIC pd_config PRIVATE pd_flags)
