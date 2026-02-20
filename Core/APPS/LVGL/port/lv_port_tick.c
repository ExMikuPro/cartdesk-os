/*********************
*      INCLUDES
 *********************/
#include "lv_port_tick.h"
#include "lvgl.h"
#include "stm32h7xx_hal.h"
#include "src/tick/lv_tick.h"

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
 * @brief Initialize LVGL tick interface
 * 使用HAL_GetTick()作为时基
 */
void lv_port_tick_init(void)
{
 /* 设置LVGL的tick回调函数 */
 lv_tick_set_cb(lv_port_tick_get);
}

/**
 * @brief Get current tick in milliseconds
 * @return Current tick value in ms
 */
uint32_t lv_port_tick_get(void)
{
 return HAL_GetTick();
}

/**
 * @brief SysTick中断回调（可选）
 * 如果不使用lv_tick_set_cb，可以在SysTick中断中调用lv_tick_inc
 *
 * 使用方法：在stm32h7xx_it.c的SysTick_Handler中添加：
 * void SysTick_Handler(void)
 * {
 *     HAL_IncTick();
 *     lv_tick_inc(1);  // 告诉LVGL过了1ms
 * }
 */