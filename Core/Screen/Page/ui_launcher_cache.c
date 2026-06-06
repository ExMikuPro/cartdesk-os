//
// Created by Hatsune Miku on 2026/2/19.
//

#include "ui_launcher_cache.h"

#include <string.h>

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

void launcher_cache_init(void)
{
    uint8_t i;

    memset(g_launcher_big_slot, 0, sizeof(g_launcher_big_slot));
    memset(g_launcher_small_icon, 0, sizeof(g_launcher_small_icon));
    memset(g_launcher_app_name, 0, sizeof(g_launcher_app_name));

    for (i = 0; i < LAUNCHER_BIG_ICON_COUNT; ++i) {
        g_launcher_order[i] = i;
    }
}

void launcher_set_order(uint8_t pos, uint8_t slot)
{
    if (pos >= LAUNCHER_BIG_ICON_COUNT || slot >= LAUNCHER_BIG_ICON_COUNT) {
        return;
    }

    g_launcher_order[pos] = slot;
}

void launcher_swap(uint8_t a, uint8_t b)
{
    uint8_t tmp;

    if (a >= LAUNCHER_BIG_ICON_COUNT || b >= LAUNCHER_BIG_ICON_COUNT) {
        return;
    }

    tmp = g_launcher_order[a];
    g_launcher_order[a] = g_launcher_order[b];
    g_launcher_order[b] = tmp;
}

uint32_t *launcher_get_big_icon(uint8_t pos)
{
    if (pos >= LAUNCHER_BIG_ICON_COUNT) {
        return NULL;
    }

    return g_launcher_big_slot[pos];
}

uint32_t *launcher_get_small_icon(uint8_t index)
{
    if (index >= LAUNCHER_SMALL_ICON_COUNT) {
        return NULL;
    }

    return g_launcher_small_icon[index];
}

const char *launcher_get_app_name(uint8_t pos)
{
    if (pos >= LAUNCHER_BIG_ICON_COUNT) {
        return NULL;
    }

    return g_launcher_app_name[pos];
}

void launcher_set_app_name(uint8_t slot, const char *name)
{
    if (slot >= LAUNCHER_BIG_ICON_COUNT) {
        return;
    }

    if (name == NULL) {
        g_launcher_app_name[slot][0] = '\0';
        return;
    }

    strncpy(g_launcher_app_name[slot], name, LAUNCHER_APP_NAME_MAX - 1u);
    g_launcher_app_name[slot][LAUNCHER_APP_NAME_MAX - 1u] = '\0';
}
