/**
 * @file lv_port_disp.c
 * @brief LVGL 9.6.0 显示驱动实现 - 支持LTDC VSync和双缓冲
 * @note  解决画面撕裂问题
 */

#include "lv_port_disp.h"
#include "lvgl.h"
#include "lcd.h"
#include "display_trace.h"
#include "perf_monitor.h"
#include "runtime_stats.h"
#include "cmsis_os2.h"

/*********************
 *      宏定义
 *********************/

/* VSync配置 */
#define VSYNC_WAIT_TIMEOUT  100  // VSync等待超时(ms)
#define LTDC_RELOAD_WAIT_TIMEOUT 100u

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
static bool g_first_flush_submit_pending = true;
static bool g_first_flush_wait_pending = true;
static bool g_first_screen_visible_pending = false;
static uint32_t g_first_screen_visible_start = 0u;
static osSemaphoreId_t g_ltdc_reload_complete_sem = NULL;
static volatile bool g_ltdc_reload_pending = false;
static volatile uint32_t g_ltdc_reload_request_seq = 0u;
static volatile uint32_t g_ltdc_reload_complete_seq = 0u;
static volatile uint32_t g_ltdc_reload_pending_fb = 0u;

/**********************
 *   静态函数声明
 **********************/
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_wait_for_vsync(void);
static void disp_flush_wait(lv_display_t *disp);
static void disp_finish_flush(lv_display_t *disp, bool notify_lvgl);
static void disp_drain_reload_completion(void);
#if CARTDESK_LTDC_SYNC_TRACE_ENABLE
static void display_trace_refresh_begin_cb(lv_event_t *event);
static void display_trace_render_begin_cb(lv_event_t *event);
static void display_trace_render_end_cb(lv_event_t *event);
#endif

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
    DisplayTrace_ConfigureFramebuffers((uint32_t)g_fb0, (uint32_t)g_fb1, (uint32_t)g_fb0);

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
    lv_display_set_flush_wait_cb(g_disp, disp_flush_wait);
    g_ltdc_reload_complete_sem = osSemaphoreNew(1u, 0u, NULL);

#if CARTDESK_LTDC_SYNC_TRACE_ENABLE
    /* These LVGL events are observers only; they do not change rendering, sync, or flush state. */
    lv_display_add_event_cb(g_disp, display_trace_refresh_begin_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(g_disp, display_trace_render_begin_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(g_disp, display_trace_render_end_cb, LV_EVENT_RENDER_READY, NULL);
#endif
    
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
    DisplayTrace_FlushEnter((uint32_t)px_map, area_px,
                            (width == 800u && height == 480u) ? 1u : 0u);

    /* 如果禁用更新，直接返回 */
    if (!g_update_enabled) {
        disp_finish_flush(disp, true);
        return;
    }

    /* DIRECT mode submits a framebuffer only for the final area.  Intermediate
     * areas must not issue a page flip or leave LVGL flushing. */
    if (!lv_display_flush_is_last(disp)) {
        disp_finish_flush(disp, true);
        return;
    }

#if USE_VSYNC
    /* 等待垂直消隐期 */
    disp_wait_for_vsync();
#endif

#if USE_DOUBLE_BUFFER
    /* Invariants:
     * 1. LTDC scanout front buffer is never written by LVGL.
     * 2. A new Layer 1 framebuffer is latched only by a VBlank reload.
     * 3. LVGL's flush lifetime ends only after that reload is acknowledged.
     * 4. The ISR only signals hardware completion; LVGL remains app-task owned. */
    extern LTDC_HandleTypeDef hltdc;
    uint32_t submit_start = PerfMonitor_Begin();

    if (g_ltdc_reload_complete_sem == NULL || g_ltdc_reload_pending) {
        /* Do not overwrite an in-flight CFBAR request.  Dropping this presentation
         * retains the current front buffer and avoids an unsafe immediate swap. */
        g_update_enabled = false;
        DisplayTrace_ReloadRejected((uint32_t)px_map);
        disp_finish_flush(disp, true);
        return;
    }

    /* Write Layer 1 shadow configuration only.  This HAL API deliberately does
     * not write SRCR=IMR; the following VBR request is the sole production flip. */
    DisplayTrace_SetAddressBegin((uint32_t)px_map);
    HAL_StatusTypeDef status = HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)px_map, 1);
    DisplayTrace_SetAddressEnd((uint32_t)px_map);
    DisplayTrace_SetAddressNoReload((uint32_t)px_map);
    if (status != HAL_OK) {
        g_update_enabled = false;
        disp_finish_flush(disp, true);
        return;
    }

    /* Discard any old completion before arming this generation.  A short critical
     * section also clears a stale RR flag before HAL enables RR for this VBR. */
    disp_drain_reload_completion();
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_RR);
    ++g_ltdc_reload_request_seq;
    g_ltdc_reload_pending_fb = (uint32_t)px_map;
    g_ltdc_reload_pending = true;
    __DMB();
    DisplayTrace_LvglReloadRequest((uint32_t)px_map);
    status = HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
    __set_PRIMASK(primask);
    if (status != HAL_OK) {
        g_ltdc_reload_pending = false;
        g_update_enabled = false;
        /* No VBR was accepted, so the new shadow address is not presented. */
        disp_finish_flush(disp, true);
        return;
    }
    if (g_first_flush_submit_pending) {
        PerfMonitor_End(PERF_MONITOR_STARTUP_FIRST_FLUSH_SUBMIT, submit_start);
        g_first_flush_submit_pending = false;
        g_first_screen_visible_start = PerfMonitor_Begin();
        g_first_screen_visible_pending = true;
    }
#else
    /* 单缓冲模式：可选的缓存一致性处理 */
    /* 如果SDRAM配置为cacheable，需要清除D-Cache */
    // SCB_CleanDCache_by_Addr((uint32_t*)area, area_size);
#endif

    /* LVGL calls disp_flush_wait() before it permits this DIRECT buffer to be
     * reused.  Do not call lv_display_flush_ready() on the presentation path. */
}

static void disp_finish_flush(lv_display_t *disp, bool notify_lvgl)
{
    DisplayTrace_FlushComplete();
    RuntimeStats_EndLvglFlush();

    ++g_frame_count;
    uint32_t current_time = HAL_GetTick();
    if (current_time - g_last_fps_time >= 1000) {
        g_current_fps = g_frame_count;
        g_frame_count = 0;
        g_last_fps_time = current_time;
    }

    if (notify_lvgl) {
        lv_display_flush_ready(disp);
    }
}

static void disp_drain_reload_completion(void)
{
    while (osSemaphoreAcquire(g_ltdc_reload_complete_sem, 0u) == osOK) {
        /* Consume stale completion tokens before arming the next generation. */
    }
}

/* LVGL 9.6 invokes this in the app/LVGL owner task and clears disp->flushing
 * only after it returns.  The callback therefore waits for the matching LTDC
 * generation and does not call lv_display_flush_ready() itself. */
static void disp_flush_wait(lv_display_t *disp)
{
    (void)disp;
    uint32_t request_seq = g_ltdc_reload_request_seq;
    uint32_t draw_fb = g_ltdc_reload_pending_fb;
    uint32_t started = HAL_GetTick();

    DisplayTrace_FlushWaitBegin(draw_fb);
    while (g_ltdc_reload_complete_seq != request_seq) {
        uint32_t elapsed = HAL_GetTick() - started;
        if (elapsed >= LTDC_RELOAD_WAIT_TIMEOUT ||
            osSemaphoreAcquire(g_ltdc_reload_complete_sem,
                               LTDC_RELOAD_WAIT_TIMEOUT - elapsed) != osOK) {
            /* If the IRQ was lost but hardware has cleared VBR, the reload was
             * applied and it is safe to finish this exact generation. */
            extern LTDC_HandleTypeDef hltdc;
            if ((hltdc.Instance->SRCR & LTDC_SRCR_VBR) == 0u && g_ltdc_reload_pending) {
                DisplayTrace_LtdcReloadEvent();
                lv_port_disp_signal_reload_complete();
                continue;
            }

            /* No IMR fallback and no synthetic completion: stop presentation and
             * reset rather than letting LVGL reuse a buffer with unknown scanout
             * ownership.  The reset is a bounded fail-safe, not a normal path. */
            g_update_enabled = false;
            DisplayTrace_ReloadWaitTimeout(draw_fb);
            NVIC_SystemReset();
            return;
        }
    }

    DisplayTrace_FlushWaitEnd(draw_fb);
    disp_finish_flush(disp, false);
}

/**
 * @brief 等待垂直同步信号
 */
static void disp_wait_for_vsync(void)
{
#if USE_VSYNC
    uint32_t first_wait_start = PerfMonitor_Begin();
    RuntimeStats_BeginLvglFlushWait();
    DisplayTrace_VsyncWaitBegin();

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
        DisplayTrace_VsyncWaitEnd(0u);
    } else {
        DisplayTrace_VsyncWaitEnd(1u);
    }

    RuntimeStats_EndLvglFlushWait();
    if (g_first_flush_wait_pending) {
        PerfMonitor_End(PERF_MONITOR_STARTUP_FIRST_FLUSH_WAIT, first_wait_start);
        g_first_flush_wait_pending = false;
    }
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
    DisplayTrace_Poll();
    g_vsync_flag = true;
    if (g_first_screen_visible_pending) {
        uint32_t elapsed = PerfMonitor_Begin() - g_first_screen_visible_start;
        PerfMonitor_Record(PERF_MONITOR_STARTUP_FIRST_PAGE_FLIP, elapsed);
        PerfMonitor_RecordFirstScreenVisible();
        g_first_screen_visible_pending = false;
    }
#endif
}

void lv_port_disp_signal_reload_complete(void)
{
#if USE_DOUBLE_BUFFER
    if (!g_ltdc_reload_pending) {
        return;
    }

    uint32_t draw_fb = g_ltdc_reload_pending_fb;
    g_ltdc_reload_complete_seq = g_ltdc_reload_request_seq;
    g_ltdc_reload_pending = false;
    __DMB();
    DisplayTrace_ReloadCompleteSignal(draw_fb);
    (void)osSemaphoreRelease(g_ltdc_reload_complete_sem);
#endif
}

#if CARTDESK_LTDC_SYNC_TRACE_ENABLE
static uint32_t display_trace_active_draw_buffer(lv_event_t *event)
{
    lv_display_t *display = lv_event_get_target(event);
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(display);
    return draw_buf != NULL ? (uint32_t)draw_buf->data : 0u;
}

static void display_trace_refresh_begin_cb(lv_event_t *event)
{
    DisplayTrace_RefreshBegin(display_trace_active_draw_buffer(event));
}

static void display_trace_render_begin_cb(lv_event_t *event)
{
    DisplayTrace_RenderBegin(display_trace_active_draw_buffer(event));
}

static void display_trace_render_end_cb(lv_event_t *event)
{
    DisplayTrace_RenderEnd(display_trace_active_draw_buffer(event));
}
#endif

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
