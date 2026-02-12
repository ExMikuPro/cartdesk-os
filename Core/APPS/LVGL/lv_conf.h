/**
 * @file lv_conf.h
 * @brief LVGL 9.4 配置文件 - 针对STM32H743优化
 * 
 * 配置说明:
 * - MCU: STM32H743 (Cortex-M7, 480MHz)
 * - RAM: 1MB SRAM + 8MB SDRAM
 * - 显示: 800x480 ARGB8888
 * - 特性: DMA2D硬件加速, 双缓冲, VBlank同步
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 32 for ARGB8888 */
#define LV_COLOR_DEPTH 32

/*=========================
   MEMORY SETTINGS
 *=========================*/

/* 使用自定义malloc/free (可选: 使用FreeRTOS的内存管理) */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* LVGL内存池大小 (字节) - STM32H743有充足的SRAM */
#define LV_MEM_SIZE    (128U * 1024U)  /* 128KB */

#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* 内存池缓冲区位置 (可选: 放到SDRAM以节省SRAM) */
// #define LV_MEM_POOL_INCLUDE your_alloc_declaration
// #define LV_MEM_POOL_ALLOC   your_alloc_function

/*=========================
   HAL SETTINGS
 *=========================*/

/* Default display refresh, input read period in milliseconds */
#define LV_DEF_REFR_PERIOD  10  /* 100fps (实际受VBlank限制到60fps) */

/* Input device read period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 10

/*=======================
   OPERATING SYSTEM
 *=======================*/

/* RTOS支持 (如果使用FreeRTOS,设置为1) */
#define LV_USE_OS   LV_OS_NONE

/*========================
   RENDERING CONFIGURATION
 *========================*/

/* 刷新模式 */
#define LV_DISPLAY_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL

/* DMA2D硬件加速 (STM32专用) */
#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_ASM            LV_DRAW_SW_ASM_NONE
    #define LV_DRAW_SW_COMPLEX        1
    #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
    #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #define LV_DRAW_SW_GRADIENT_CACHE_SIZE 1
#define LV_USE_DRAW_DMA2D               1
#define LV_DRAW_DMA2D_HAL_INCLUDE       "stm32h7xx_hal.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT     1
#endif

/*=================
   FONT USAGE
 ==================*/

/* Montserrat fonts with various styles and bpp */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Default font */
// #define LV_FONT_DEFAULT &lv_font_montserrat_14

/* 中文字体支持 (需要自行添加字体文件) */
#define LV_FONT_CUSTOM_DECLARE
// LV_FONT_DECLARE(my_chinese_font_16)

/*===================
   TEXT SETTINGS
 *===================*/

/* 文本编码 */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/* 文本选择支持 */
#define LV_TXT_BREAK_CHARS " ,.;:-_"

#define LV_USE_ST_LTDC 1

/*=================
   WIDGET USAGE
 ==================*/

/* 启用所有常用控件 */
#define LV_USE_ANIMIMG      1
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR     1
#define LV_USE_CANVAS       1
#define LV_USE_CHART        1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  1
#define LV_USE_KEYBOARD     1
#define LV_USE_LABEL        1
#define LV_USE_LED          1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       1
#define LV_USE_SCALE        1
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      1
#define LV_USE_TEXTAREA     1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/*==================
   LAYOUTS
 *==================*/
#define LV_USE_FLEX     1
#define LV_USE_GRID     1

/*==================
   THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/*==================
   OTHERS
 *==================*/

/* API日志 (调试用) */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    /* 日志输出函数 (使用printf输出到串口) */
    // #define LV_LOG_USER_CB(msg) printf("%s\n", msg)
#endif

/* 断言 */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* 用户数据 */
#define LV_USE_USER_DATA 1

/*==================
   EXAMPLES
 *==================*/
#define LV_BUILD_EXAMPLES 0

/*==================
   DEMOS
 *==================*/
#define LV_USE_DEMO_WIDGETS    1
#define LV_USE_DEMO_BENCHMARK  0
#define LV_USE_DEMO_STRESS     0
#define LV_USE_DEMO_MUSIC      0

/** 1: Enable system monitor component */
#define LV_USE_SYSMON   0
#if LV_USE_SYSMON
    /** Get the idle percentage. E.g. uint32_t my_get_idle(void); */
    #define LV_SYSMON_GET_IDLE lv_os_get_idle_percent
    /** 1: Enable usage of lv_os_get_proc_idle_percent.*/
    #define LV_SYSMON_PROC_IDLE_AVAILABLE 0
    #if LV_SYSMON_PROC_IDLE_AVAILABLE
        /** Get the applications idle percentage.
         * - Requires `LV_USE_OS == LV_OS_PTHREAD` */
        #define LV_SYSMON_GET_PROC_IDLE lv_os_get_proc_idle_percent
    #endif

    /** 1: Show CPU usage and FPS count.
     *  - Requires `LV_USE_SYSMON = 1` */
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

        /** 0: Displays performance data on the screen; 1: Prints performance data using log. */
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    /** 1: Show used memory and memory fragmentation.
     *     - Requires `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN`
     *     - Requires `LV_USE_SYSMON = 1`*/
    #define LV_USE_MEM_MONITOR 0
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif
#endif /*LV_USE_SYSMON*/

#endif /* LV_CONF_H */
