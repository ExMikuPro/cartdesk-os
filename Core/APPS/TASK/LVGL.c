#include "cartdesk_task.h"
#include "misc/lv_timer.h"

uint32_t TaskTicks_LVGL = 0;

void Task_LVGL() {
  if (TaskTicks_LVGL == 0) {
    lv_timer_handler();
    TaskTicks_LVGL = 10;
  }
}
