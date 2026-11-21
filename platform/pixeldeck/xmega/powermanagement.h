#ifndef _LALEMA_POWERMANAGEMENT_H_
#define _LALEMA_POWERMANAGEMENT_H_

#include <stdbool.h>
#include <stdint.h>

void PowerLatch(void);

void PowerWorker(void);

// Takt für Statuspakete und Power-Taste (pixeldeck M3)
#define STATUS_TICK_HZ 200
#define PWR_DEBOUNCE_TICKS 4    // 20 ms
#define PWR_GRACE_TICKS 300     // 1,5 s Zeit für den ESP zum Sichern
#define PWR_FALLBACK_TICKS 400  // 2 s nach dem Loslassen noch Strom: weiterlaufen

void PowerTick(void);           // mit STATUS_TICK_HZ aufrufen
bool isShutdownPending(void);   // Power-Taste gedrückt, Abschalten läuft

uint16_t getBatteryLevel(void);

bool isPowerButtonPressed(void);
bool isDeviceCharging(void);

#endif
