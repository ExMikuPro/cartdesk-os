//
// Created by Hatsune Miku on 2026/2/13.
//

#ifndef CARTDESK_TASK_H
#define CARTDESK_TASK_H
#include <main.h>

extern uint32_t TaskTicks_LED;
extern uint32_t TaskTicks_LVGL;
extern uint32_t TaskTicks_LUA;

void Task_LED();
void Task_LVGL();
void Task_LUA();
void Task_LUA_StartCart(const char *cart_path);
uint8_t Task_LUA_IsRunning(void);





#endif //CARTDESK_TASK_H
