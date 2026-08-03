#include "app_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "audio_task.h"
#include "cart_io_service.h"
#include "cart_log.h"
#include "cmsis_os2.h"
#include "flash.h"
#include "lcd.h"
#include "lua_runtime_task.h"
#include "lua_foundation.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "lvgl_init.h"
#include "main.h"
#include "perf_monitor.h"
#include "qflash_font.h"
#include "runtime_stats.h"
#include "task.h"
#include "ui_screen_launcher.h"
#if XHGC_MEM_OVERLAY_ENABLE
#include "xhgc_mem_overlay.h"
#endif

#define APP_TASK_PERIOD_MS 5u
#define CARTDESK_ENABLE_QFLASH_FONT 1
#define APP_COMPLETION_BUDGET 8u

static cart_task_stats_t s_app_stats;

static void process_worker_completions(void)
{
    uint32_t handled = 0u;
    cart_io_completion_t io_completion;
    while(handled < APP_COMPLETION_BUDGET &&
          CartIoService_TryReceive(&io_completion)) {
        if(!lua_foundation_handle_io_completion(&io_completion) &&
           !Launcher_HandleIoCompletion(&io_completion)) {
            if(io_completion.operation == CART_IO_OP_STORAGE_LOAD ||
               io_completion.operation == CART_IO_OP_STORAGE_COMMIT ||
               io_completion.operation == CART_IO_OP_STORAGE_CLEAR) {
                CartTaskBuffer_Release(&io_completion.result.storage.buffer);
            }
            ++s_app_stats.stale_completion;
        }
        ++handled;
    }

    cart_audio_completion_t audio_completion;
    while(handled < APP_COMPLETION_BUDGET &&
          CartdeskAudioTask_TryReceive(&audio_completion)) {
        (void)audio_completion;
        ++handled;
    }
}

void CartdeskAppTask_GetStats(cart_task_stats_t *stats)
{
    if(stats != NULL) *stats = s_app_stats;
}

#if CARTDESK_ENABLE_QFLASH_FONT
static void qflash_font_init_or_fallback(void)
{
    uint32_t qflash_start = PerfMonitor_Begin();
    if (!CartIoService_WaitReady(CART_IO_READY_TIMEOUT_MS)) {
        PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_INIT, qflash_start);
        CartLog_Write(CART_LOG_WARN, "app",
                      "IO startup timeout; using built-in font and degraded storage");
        return;
    }

    const void *font_pack =
        (const void *)(uintptr_t)(FLASH_MM_BASE + QFLASH_FONT_PACK_OFFSET);
    PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_INIT, qflash_start);

    uint32_t mount_start = PerfMonitor_Begin();
    bool mounted = QFlashFont_Mount(font_pack, QFLASH_FONT_REGION_SIZE);
    PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_MOUNT, mount_start);
    if (!mounted) {
        CartLog_Write(CART_LOG_WARN, "app", QFlashFont_LastError());
    } else {
        QFlashFontStorageInfo storage_info;
        if (QFlashFont_GetStorageInfo(&storage_info)) {
            char message[128];
            (void)snprintf(message, sizeof(message),
                           "QFLASH font mounted: default=%u storage=%lu/%lu",
                           (unsigned)QFLASH_FONT_DEFAULT_SIZE,
                           (unsigned long)storage_info.used_bytes,
                           (unsigned long)storage_info.capacity_bytes);
            CartLog_Write(CART_LOG_INFO, "app", message);
        }
    }
}
#endif

static void delay_until_next_period(uint32_t *next_wake)
{
    uint32_t now = osKernelGetTickCount();
    *next_wake += APP_TASK_PERIOD_MS;
    if ((int32_t)(*next_wake - now) <= 0) {
        *next_wake = now + APP_TASK_PERIOD_MS;
    }
    (void)osDelayUntil(*next_wake);
}

void CartdeskAppTask_Run(void *argument)
{
    (void)argument;
    bool first_lv_timer_pending = true;

    RuntimeStats_Init();
#if CARTDESK_ENABLE_QFLASH_FONT
    qflash_font_init_or_fallback();
#endif
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    LCD_DisplayON();
    Launcher_Init();
#if XHGC_MEM_OVERLAY_ENABLE
    xhgc_mem_overlay_init();
#endif

    uint32_t next_wake = osKernelGetTickCount();
    for (;;) {
        RuntimeStats_BeginSection(RUNTIME_STATS_SECTION_FRAME);
        process_worker_completions();

        if(CartIoService_IsQflashExclusive()) {
            ++s_app_stats.heartbeat;
            s_app_stats.last_heartbeat_tick = osKernelGetTickCount();
            s_app_stats.stack_high_water =
                (uint32_t)uxTaskGetStackHighWaterMark(NULL);
            RuntimeStats_EndSection(RUNTIME_STATS_SECTION_FRAME);
            RuntimeStats_UpdateSnapshot();
            delay_until_next_period(&next_wake);
            continue;
        }

        RuntimeStats_BeginSection(RUNTIME_STATS_SECTION_LVGL);
        uint32_t lv_timer_start = PerfMonitor_Begin();
        lvgl_task_handler();
        if (first_lv_timer_pending) {
            PerfMonitor_End(PERF_MONITOR_STARTUP_FIRST_LV_TIMER, lv_timer_start);
            first_lv_timer_pending = false;
        }
        RuntimeStats_EndSection(RUNTIME_STATS_SECTION_LVGL);

        RuntimeStats_BeginSection(RUNTIME_STATS_SECTION_LUA);
        uint32_t lua_start = PerfMonitor_Begin();
        LuaRuntimeTask_Process(osKernelGetTickCount());
        PerfMonitor_End(PERF_MONITOR_RUNTIME_LUA_UPDATE, lua_start);
        RuntimeStats_EndSection(RUNTIME_STATS_SECTION_LUA);

        RuntimeStats_BeginSection(RUNTIME_STATS_SECTION_LAUNCHER);
        Launcher_Task();
        RuntimeStats_EndSection(RUNTIME_STATS_SECTION_LAUNCHER);
#if XHGC_MEM_OVERLAY_ENABLE
        xhgc_mem_overlay_update();
#endif

        RuntimeStats_EndSection(RUNTIME_STATS_SECTION_FRAME);
        ++s_app_stats.heartbeat;
        s_app_stats.last_heartbeat_tick = osKernelGetTickCount();
        s_app_stats.stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        RuntimeStats_UpdateSnapshot();
        delay_until_next_period(&next_wake);
    }
}
