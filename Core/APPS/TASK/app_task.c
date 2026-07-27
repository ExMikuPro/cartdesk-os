#include "app_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "flash.h"
#include "lcd.h"
#include "lua_runtime_task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "lvgl_init.h"
#include "main.h"
#include "perf_monitor.h"
#include "qflash_font.h"
#include "quadspi.h"
#include "runtime_stats.h"
#include "ui_screen_launcher.h"
#if XHGC_MEM_OVERLAY_ENABLE
#include "xhgc_mem_overlay.h"
#endif

#define APP_TASK_PERIOD_MS 5u
#define CARTDESK_ENABLE_QFLASH_FONT 1

#if CARTDESK_ENABLE_QFLASH_FONT
static FLASH_Handle s_qflash;

static void qflash_font_init_or_fallback(void)
{
    uint32_t qflash_start = PerfMonitor_Begin();
    FLASH_Status status = FLASH_Open(&s_qflash, &hqspi, 64u * 1024u * 1024u);
    if (status == FLASH_OK) {
        status = FLASH_BringUp(&s_qflash);
    }
    if (status == FLASH_OK) {
        status = FLASH_EnableMemoryMapped(&s_qflash);
    }

    if (status != FLASH_OK) {
        PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_INIT, qflash_start);
        const FLASH_ErrorInfo *error = FLASH_LastError(&s_qflash);
        printf("QFLASH font init failed: step=%s code=%d hal=%d qspi=0x%08lx; using built-in font\r\n",
               error && error->step ? error->step : "?",
               error ? error->code : -1,
               error ? error->hal : -1,
               error ? (unsigned long)error->qspi_error : 0ul);
        return;
    }

    const void *font_pack =
        (const void *)(uintptr_t)(FLASH_MM_BASE + QFLASH_FONT_PACK_OFFSET);
    PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_INIT, qflash_start);

    uint32_t mount_start = PerfMonitor_Begin();
    bool mounted = QFlashFont_Mount(font_pack, QFLASH_FONT_REGION_SIZE);
    PerfMonitor_End(PERF_MONITOR_STARTUP_QFLASH_MOUNT, mount_start);
    if (!mounted) {
        printf("QFLASH font mount failed: %s; using built-in font\r\n",
               QFlashFont_LastError());
        return;
    }

    QFlashFontStorageInfo storage_info;
    if (QFlashFont_GetStorageInfo(&storage_info)) {
        printf("QFLASH font mounted: 16/20/24 px, default=%u px, storage=%lu/%lu bytes\r\n",
               (unsigned)QFLASH_FONT_DEFAULT_SIZE,
               (unsigned long)storage_info.used_bytes,
               (unsigned long)storage_info.capacity_bytes);
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
        RuntimeStats_UpdateSnapshot();
        RuntimeStats_PrintEveryMs(1000u);
        delay_until_next_period(&next_wake);
    }
}
