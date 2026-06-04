/**
 * @file lv_port_disp.h
 * @brief LVGL 9.5 显示驱动接口 - 支持LTDC VSync
 */

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      包含文件
 *********************/
#include "lvgl.h"
#include "stm32h7xx_hal.h"

/*********************
 *      宏定义
 *********************/

/* VSync功能开关 */
#define USE_VSYNC           1    // 启用垂直同步
#define USE_DOUBLE_BUFFER   1    // 启用双缓冲

/**********************
 *      类型定义
 **********************/

/**********************
 *   全局函数声明
 **********************/

/**
 * @brief 初始化LVGL显示驱动
 * @note  配置LTDC、双缓冲和VSync
 */
void lv_port_disp_init(void);

/**
 * @brief 启用显示更新
 * @note  允许LVGL刷新屏幕
 */
void disp_enable_update(void);

/**
 * @brief 禁用显示更新
 * @note  阻止LVGL刷新屏幕（例如在执行关键操作时）
 */
void disp_disable_update(void);

/**
 * @brief 等待垂直消隐期
 * @note  确保在VBlank期间切换缓冲区，避免撕裂
 */
void disp_wait_vsync(void);

/**
 * @brief 获取当前帧率
 * @return 当前FPS值
 */
uint32_t disp_get_fps(void);

/**
 * @brief 通知LVGL移植层已进入VSync/LineEvent阶段
 * @note  供HAL_LTDC_LineEventCallback直接调用，避免在IRQ尾部二次读硬件标志
 */
void lv_port_disp_signal_vsync(void);

/**
 * @brief LTDC行中断回调
 * @note  兼容旧调用点，保留为空实现
 */
void LTDC_IRQHandler_Callback(void);

/**********************
 *      宏函数
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_PORT_DISP_H */
