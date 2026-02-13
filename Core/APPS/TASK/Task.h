//
// Created by Hatsune Miku on 2026/2/13.
//

#ifndef XIH6_DISPLAY_TASK_H
#define XIH6_DISPLAY_TASK_H
#include <main.h>

extern uint32_t TaskTicks_LED;
extern uint32_t TaskTicks_LVGL;

void Task_LED();
void Task_LVGL();





#endif //XIH6_DISPLAY_TASK_H