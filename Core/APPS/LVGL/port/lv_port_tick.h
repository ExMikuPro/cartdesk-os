/**
 * @file lv_port_tick.h
 * @brief LVGL 9.5.0 系统时钟驱动头文件
 */

#ifndef LV_PORT_TICK_H
#define LV_PORT_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化LVGL时钟驱动
 */
void lv_port_tick_init(void);

/**
 * @brief 获取系统时间（毫秒）
 * @return 系统运行时间（ms）
 */
uint32_t lv_port_tick_get(void);

/**
 * @brief LVGL tick处理函数
 * @param period_ms 距离上次调用的时间间隔（ms）
 */
void lv_port_tick_handler(uint32_t period_ms);

/**
 * @brief 延时函数（毫秒）
 * @param ms 延时时间（毫秒）
 */
void lv_port_delay_ms(uint32_t ms);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_TICK_H*/
