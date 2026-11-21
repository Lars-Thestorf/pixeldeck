#pragma once
// Interne Schnittstellen der ESP32-Plattform (nicht Teil der HAL).
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace pd::esp {

// app_main.cpp
extern TaskHandle_t g_mainTask;

// hal_display.cpp
bool displayHwInit();
extern uint32_t g_presentUsLast, g_presentUsMax;

// hal_link.cpp: UART0-RX (xmega-Statuspakete und Dev-Kanal)
void linkStart();
void devSnapshotFrame(const uint16_t* fb);  // letztes Bild für "!fb"
void devMainTaskHook();                      // "!hang"/"!crash" im Kontext der Hauptschleife

// hal_system.cpp
void resetInfoInit();  // Neustartgrund und RTC-Daten beim Boot auswerten
void seedInit();       // Entropie sammeln, bevor I2S/DMA laufen

}  // namespace pd::esp
