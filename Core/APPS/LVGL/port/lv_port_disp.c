/**
 * @file lv_port_disp.c
 * @brief LVGL 9.5.0 显示驱动实现 - 支持LTDC VSync和双缓冲
 * @note  解决画面撕裂问题
 */

#include "lv_port_disp.h"
#include "lvgl.h"
#include "lcd.h"
#include "runtime_stats.h"

/*********************
 *      宏定义
 *********************/

/* VSync配置 */
#define VSYNC_WAIT_TIMEOUT  100  // VSync等待超时(ms)

/**********************
 *      类型定义
 **********************/

/**********************
 *   静态变量
 **********************/
static lv_display_t *g_disp = NULL;
static volatile bool g_vsync_flag = false;      // VSync中断标志
static volatile bool g_update_enabled = true;   // 更新使能标志
static volatile uint32_t g_frame_count = 0;     // 帧计数器
static uint32_t g_last_fps_time = 0;            // 上次FPS计算时间
static uint32_t g_current_fps = 0;              // 当前FPS

/* 双缓冲指针 */
static void *g_fb0 = NULL;  // 前台缓冲（显示缓冲）
static void *g_fb1 = NULL;  // 后台缓冲（绘制缓冲）

/**********************
 *   静态函数声明
 **********************/
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_wait_for_vsync(void);

/**********************
 *   全局函数实现
 **********************/

/**
 * @brief 初始化LVGL显示驱动
 */
void lv_port_disp_init(void)
{
    /* 统一由 LCD 驱动初始化 LTDC 双缓冲和 VBlank LineEvent，避免和 LVGL flush 的翻页逻辑失配。 */
#if USE_VSYNC || USE_DOUBLE_BUFFER
    LCD_DoubleBufferInit();
#endif

    /* 获取LCD帧缓冲地址 */
    g_fb0 = (void *)LCD_GetFB(1);      // Layer1的显示缓冲
    g_fb1 = (void *)LCD_GetDrawFB(1);  // Layer1的绘制缓冲

    extern LTDC_HandleTypeDef hltdc;
    uint32_t width = hltdc.LayerCfg[1].ImageWidth;
    uint32_t height = hltdc.LayerCfg[1].ImageHeight;

    g_disp = lv_display_create(width, height);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_ARGB8888);

#if USE_DOUBLE_BUFFER
    /* 恢复 DIRECT 双缓冲，保留局部刷新性能；图片 DMA2D 已单独收敛，避免再走有问题的 image/blend 路径。 */
    lv_display_set_buffers(g_disp, g_fb0, g_fb1,
                          width * height * 4,
                          LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(g_disp, g_fb0, NULL,
                          width * height * 4,
                          LV_DISPLAY_RENDER_MODE_DIRECT);
#endif
    lv_display_set_flush_cb(g_disp, disp_flush);
    
    /* 设置为默认显示 */
    lv_display_set_default(g_disp);

#if USE_VSYNC
    /* LTDC LineEvent 已由 LCD_DoubleBufferInit 配置到 VBlank；这里只保留中断优先级约束。 */
    HAL_NVIC_SetPriority(LTDC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
#endif

    /* 初始化FPS计时器 */
    g_last_fps_time = HAL_GetTick();
}

/**
 * @brief 显示刷新回调函数
 * @param disp 显示对象
 * @param area 刷新区域
 * @param px_map 像素数据指针
 */
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t area_px = 0u;

    if (area != NULL && area->x2 >= area->x1 && area->y2 >= area->y1) {
        width = (uint32_t)(area->x2 - area->x1 + 1);
        height = (uint32_t)(area->y2 - area->y1 + 1);
        area_px = width * height;
    }
    RuntimeStats_BeginLvglFlush(area_px);

    /* 如果禁用更新，直接返回 */
    if (!g_update_enabled) {
        lv_display_flush_ready(disp);
        RuntimeStats_EndLvglFlush();
        return;
    }

#if USE_VSYNC
    /* 等待垂直消隐期 */
    disp_wait_for_vsync();
#endif

#if USE_DOUBLE_BUFFER
    /* 双缓冲模式：DIRECT 渲染下，px_map 指向 LVGL 刚刚渲染完成的整屏 buffer。
     * 直接把 LTDC Layer1 指到 px_map，避免和 LVGL 的 buffer 轮换逻辑不同步。 */
    extern LTDC_HandleTypeDef hltdc;

    /* 切换到 LVGL 刚渲染完成的 buffer */
    HAL_LTDC_SetAddress(&hltdc, (uint32_t)px_map, 1);

    /* 等待地址重载完成 */
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
#else
    /* 单缓冲模式：可选的缓存一致性处理 */
    /* 如果SDRAM配置为cacheable，需要清除D-Cache */
    // SCB_CleanDCache_by_Addr((uint32_t*)area, area_size);
#endif

    /* 通知LVGL刷新完成 */
    lv_display_flush_ready(disp);
    RuntimeStats_EndLvglFlush();

    /* 更新帧计数 */
    g_frame_count++;

    /* 每秒计算一次FPS */
    uint32_t current_time = HAL_GetTick();
    if (current_time - g_last_fps_time >= 1000) {
        g_current_fps = g_frame_count;
        g_frame_count = 0;
        g_last_fps_time = current_time;
    }
}

/**
 * @brief 等待垂直同步信号
 */
static void disp_wait_for_vsync(void)
{
#if USE_VSYNC
    RuntimeStats_BeginLvglFlushWait();

    /* 清除标志 */
    g_vsync_flag = false;

    /* 等待VSync中断 */
    uint32_t timeout = HAL_GetTick() + VSYNC_WAIT_TIMEOUT;
    while (!g_vsync_flag && (HAL_GetTick() < timeout)) {
        __NOP();  // 空操作，等待中断
    }

    /* 超时处理 */
    if (!g_vsync_flag) {
        // VSync超时，可以记录日志或采取其他措施
    }

    RuntimeStats_EndLvglFlushWait();
#endif
}

/**
 * @brief 公共的VSync等待接口
 */
void disp_wait_vsync(void)
{
    disp_wait_for_vsync();
}

/**
 * @brief 启用显示更新
 */
void disp_enable_update(void)
{
    g_update_enabled = true;
}

/**
 * @brief 禁用显示更新
 */
void disp_disable_update(void)
{
    g_update_enabled = false;
}

/**
 * @brief 获取当前FPS
 * @return FPS值
 */
uint32_t disp_get_fps(void)
{
    return g_current_fps;
}

/**
 * @brief 通知LVGL移植层当前已进入VSync/LineEvent阶段
 */
void lv_port_disp_signal_vsync(void)
{
#if USE_VSYNC
    g_vsync_flag = true;
#endif
}

/**
 * @brief LTDC中断回调函数
 * @note  兼容旧调用点，实际VSync通知已在HAL_LTDC_LineEventCallback中完成
 */
void LTDC_IRQHandler_Callback(void)
{
#if USE_VSYNC
    /* VSync 标志已在 HAL_LTDC_LineEventCallback 里直接置位，这里不再二次读/清硬件标志。 */
#endif
}
