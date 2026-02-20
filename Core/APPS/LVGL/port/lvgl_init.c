/*********************
*      INCLUDES
 *********************/
#include "lvgl_init.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_tick.h"
#include "lv_port_indev.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * @brief 初始化LVGL及所有移植接口
 * @note 在main函数中调用，在初始化外设之后
 */
void lvgl_init(void)
{
  /* 1. 初始化LVGL核心 */
  lv_init();

  /* 2. 初始化时基 */
  lv_port_tick_init();

  /* 3. 初始化显示驱动 */
  lv_port_disp_init();

  /* 4. 初始化输入设备（触摸屏暂不启用） */
  lv_port_indev_init();
}

/**
 * @brief LVGL任务处理函数
 * @note 需要在主循环中周期性调用，建议5-10ms调用一次
 *
 * 使用示例：
 * while(1) {
 *     lvgl_task_handler();
 *     HAL_Delay(5);
 * }
 */
void lvgl_task_handler(void)
{
  lv_timer_handler();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/