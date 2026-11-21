# pixeldeck für ESP32 (ESP-IDF 5.5). Bauen: siehe README.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# HUB75-Lib: ohne Adafruit-GFX (pixeldeck zeichnet selbst) und ohne CIE-Tabelle
# (Gamma macht die Display-HAL, PD_PANEL_GAMMA).
idf_build_set_property(COMPILE_DEFINITIONS "NO_GFX" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "NO_CIE1931" APPEND)
# 3.1.0 setzt den Wert nicht selbst; ohne Define bricht der Header ab.
idf_build_set_property(COMPILE_DEFINITIONS "PIXEL_COLOR_DEPTH_BITS=8" APPEND)

project(pixeldeck)
