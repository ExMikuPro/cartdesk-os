/*********************
*      INCLUDES
 *********************/
#include "lvgl_init.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_tick.h"
#include "lv_port_indev.h"
#include "runtime_stats.h"

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * @brief 初始化LVGL及所有移植接口
 * @note 在main函数中调用，在初始化外设之后
 */
void lvgl_init(void)
{
  /* v9.6 DMA2D backend now owns its D-Cache handler registration. */
  lv_init();
  lv_port_tick_init();
  lv_port_disp_init();
  lv_port_indev_init();
}

/**
 * @brief LVGL任务处理函数
 * @note 需要在主循环中周期性调用，建议5-10ms调用一次
 */
void lvgl_task_handler(void)
{
  RuntimeStats_BeginLvglTimer();
  lv_timer_handler();
  RuntimeStats_EndLvglTimer();
}
