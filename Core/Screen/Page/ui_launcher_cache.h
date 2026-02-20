//
// Created by Hatsune Miku on 2026/2/19.
//

#pragma once
#include "main.h"

#ifndef XIH6_DISPLAY_UI_LAUNCHER_CACHE_H
#define XIH6_DISPLAY_UI_LAUNCHER_CACHE_H

#define LAUNCHER_BIG_ICON_COUNT   12
#define LAUNCHER_BIG_ICON_W       200
#define LAUNCHER_BIG_ICON_H       200

#define LAUNCHER_SMALL_ICON_COUNT 5
#define LAUNCHER_SMALL_ICON_W     48
#define LAUNCHER_SMALL_ICON_H     48

#define LAUNCHER_APP_NAME_MAX     64

extern uint32_t g_launcher_big_slot[LAUNCHER_BIG_ICON_COUNT][LAUNCHER_BIG_ICON_W * LAUNCHER_BIG_ICON_H];
extern uint32_t g_launcher_small_icon[LAUNCHER_SMALL_ICON_COUNT][LAUNCHER_SMALL_ICON_W * LAUNCHER_SMALL_ICON_H];

extern uint8_t  g_launcher_order[LAUNCHER_BIG_ICON_COUNT];
extern char     g_launcher_app_name[LAUNCHER_BIG_ICON_COUNT][LAUNCHER_APP_NAME_MAX];


void launcher_cache_init(void);

void launcher_set_order(uint8_t pos, uint8_t slot);
void launcher_swap(uint8_t a, uint8_t b);

uint32_t* launcher_get_big_icon(uint8_t pos);
uint32_t* launcher_get_small_icon(uint8_t index);

const char* launcher_get_app_name(uint8_t pos);
void launcher_set_app_name(uint8_t slot, const char* name);



#endif //XIH6_DISPLAY_UI_LAUNCHER_CACHE_H