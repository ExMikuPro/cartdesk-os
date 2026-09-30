/**
 * @file lv_port_disp.h
 * @brief LVGL 9.6.0 显示驱动接口 - 支持LTDC VSync
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

#include <stdbool.h>

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
 * @brief 由 LTDC reload IRQ 确认最近一次 LVGL VBlank presentation 已生效
 * @note  仅执行序号更新和 ISR-safe semaphore signaling；不调用 LVGL API。
 */
void lv_port_disp_signal_reload_complete(void);

/**
 * @brief LTDC行中断回调
 * @note  兼容旧调用点，保留为空实现
 */
void LTDC_IRQHandler_Callback(void);

/**
 * @brief  当前是否没有在飞行中的 LVGL presentation（可以安全切换显示模式）
 * @retval true=没有未完成的 CFBAR/reload 请求
 */
bool lv_port_disp_is_flush_idle(void);

/**
 * @brief  注册 LVGL flush 落地前的钩子
 * @param  hook 在写 CFBAR 之前、同一个 VBR 之前调用；NULL 注销
 * @param  user 回调上下文
 * @note   Launcher -> Lua 的原子切换依赖这个钩子：第一次 LVGL flush 必须同时
 *         恢复 HW Layer1 全屏几何并关闭 HW Layer0，与 LVGL 自己的 CFBAR 更新
 *         在同一次 VBR 内生效，避免出现几何错乱或旧 Launcher 画面的一帧。
 */
void lv_port_disp_set_pre_latch_hook(void (*hook)(void *user), void *user);

/**
 * @brief  注册 presentation 门控
 * @param  gate 返回 false 时 disp_flush 不修改 CFBAR、不请求 VBR，只结束本次
 *              flush；NULL 表示不门控
 * @note   Launcher 硬件平移模式下 HW Layer1 由 pan 状态机独占，任何 LVGL
 *         presentation 都会破坏 strip 几何，因此必须在此拦掉。
 */
void lv_port_disp_set_presentation_gate(bool (*gate)(void *user), void *user);

/**********************
 *      宏函数
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_PORT_DISP_H */
