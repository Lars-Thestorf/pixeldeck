#pragma once
// Konfigurationswerte (SPEC 16.4). Per CMake überschreibbar (-DPD_ARENA_SIZE=...).

#ifndef PD_ARENA_SIZE
#define PD_ARENA_SIZE (32 * 1024)
#endif
#ifndef PD_SAVE_MAX
#define PD_SAVE_MAX (4 * 1024)
#endif
#ifndef PD_MAIN_STACK_SIZE
#define PD_MAIN_STACK_SIZE (16 * 1024)
#endif
#ifndef PD_TICK_HZ
#define PD_TICK_HZ 60
#endif
#ifndef PD_WATCHDOG_MS
#define PD_WATCHDOG_MS 5000
#endif
#ifndef PD_SLOT_HOLD_MS
#define PD_SLOT_HOLD_MS 10000
#endif
#ifndef PD_AUDIO_RATE
#define PD_AUDIO_RATE 22050
#endif
#ifndef PD_MAX_PROFILES
#define PD_MAX_PROFILES 16
#endif
