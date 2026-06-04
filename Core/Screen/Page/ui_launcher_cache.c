//
// Created by Hatsune Miku on 2026/2/19.
//

#include "ui_launcher_cache.h"

#if defined(__APPLE__)
#define LAUNCHER_CACHE_SECTION __attribute__((aligned(32)))
#else
#define LAUNCHER_CACHE_SECTION __attribute__((section(".launcher_cache"), aligned(32)))
#endif

LAUNCHER_CACHE_SECTION
uint32_t g_launcher_big_slot[12][200 * 200];

LAUNCHER_CACHE_SECTION
uint32_t g_launcher_small_icon[5][48 * 48];

LAUNCHER_CACHE_SECTION
char g_launcher_app_name[12][64];

uint8_t g_launcher_order[12];
