#include "Task.h"
#include "misc/lv_timer.h"

uint32_t TaskTicks_LVGL = 0;

uint8_t LVGL_Flag = 0;

void Task_LVGL() {
  switch (LVGL_Flag) {
    case 0:
      if (TaskTicks_LVGL == 0) {
        lv_timer_handler();
        LVGL_Flag = 1;
        TaskTicks_LVGL = 5;
      }
      break;
    case 1:
      if (TaskTicks_LVGL == 0) {
        LVGL_Flag = 0;
        TaskTicks_LVGL = 0;
      }
      break;

  }
}