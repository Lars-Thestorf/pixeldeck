# PC-Simulator (SDL3)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  if(NOT PD_FETCH_SDL3)
    message(FATAL_ERROR "SDL3 nicht gefunden (libsdl3-dev installieren oder PD_FETCH_SDL3=ON)")
  endif()
  message(STATUS "pixeldeck: SDL3 nicht installiert, hole SDL 3.2.10 per FetchContent")
  include(FetchContent)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.10/SDL3-3.2.10.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

add_executable(pixeldeck
  main.cpp
  sdl_hal.cpp
  heap_guard.cpp
  png.cpp
  storage_pc.cpp
  ${PD_GAME_REGISTRY}
)
target_link_libraries(pixeldeck PRIVATE pd_core pd_flags ${PD_GAME_TARGETS} SDL3::SDL3)
set_target_properties(pixeldeck PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
