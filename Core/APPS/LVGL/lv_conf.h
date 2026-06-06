/**
 * @file lv_conf.h
 * @brief LVGL 9.5 配置文件 - 针对STM32H743优化
 *
 * 配置说明:
 * - MCU: STM32H743 (Cortex-M7, 480MHz)
 * - RAM: 1MB SRAM + 64MB SDRAM
 * - 显示: 800x480 ARGB8888
 * - 特性: DMA2D硬件加速, 双缓冲, VBlank同步
 */



#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COMPILER SETTINGS
 *====================*/

/*
 * 关键修复（你现在的 HardFault 根因）：
 * 当 LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN 时，LVGL 会使用自己的内存池。
 * 如果内存池未按字对齐，在 Cortex-M7 上会触发 UNALIGNED UsageFault（CFSR bit24），
 * 最终进入 HardFault_Handler。
 *
 * 这里统一对齐到 32 字节：同时对 DMA2D/D-Cache 也更友好。
 */
#ifndef LV_ATTRIBUTE_MEM_ALIGN_SIZE
#  define LV_ATTRIBUTE_MEM_ALIGN_SIZE 32
#endif
#ifndef LV_ATTRIBUTE_MEM_ALIGN
#  define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))
#endif
#ifndef LV_ATTRIBUTE_LARGE_RAM_ARRAY
#  if defined(__APPLE__)
#    define LV_ATTRIBUTE_LARGE_RAM_ARRAY __attribute__((aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))
#  else
#    define LV_ATTRIBUTE_LARGE_RAM_ARRAY __attribute__((section(".lvgl_heap"), aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))
#  endif
#endif

/* draw buffer/stride 对齐（DMA2D & M7 更稳） */
#ifndef LV_DRAW_BUF_ALIGN
#  define LV_DRAW_BUF_ALIGN 32
#endif
#ifndef LV_DRAW_BUF_STRIDE_ALIGN
#  define LV_DRAW_BUF_STRIDE_ALIGN 32
#endif

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 32 for ARGB8888 */
#define LV_COLOR_DEPTH 32

/*=========================
   MEMORY SETTINGS
 *=========================*/

/* 使用 LVGL 内建 malloc，把核心对象内存固定放在片内 RAM。 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*
 * 让 LVGL 在片内 RAM 中生成静态内存池。
 * 不再把 LVGL 核心堆绑定到外部 SDRAM，避免主题/对象初始化阶段访问外部内存。
 */
#define LV_MEM_ADR 0U

/* LVGL内存池大小 (字节) */
#define LV_MEM_SIZE (256U * 1024U)

/* 片内 RAM 有限，缓存控制在较小规模。 */
#define LV_CACHE_DEF_SIZE (128U * 1024U)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 32

#define LV_USE_RLE 1

#define LV_FONT_DEFAULT &lv_font_montserrat_20

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

#define LV_BIN_DECODER_RAM_LOAD 1

/* 刷新模式：你实际用的是 Layer1 双缓冲 + 直接换 FB，所以用 DIRECT */
#define LV_DISPLAY_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_ASM               LV_DRAW_SW_ASM_NONE
    #define LV_DRAW_SW_COMPLEX           1
    #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
    #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #define LV_DRAW_SW_GRADIENT_CACHE_SIZE 1
#endif

/* DMA2D 绘制加速：direct + LTDC 双缓冲场景下由 LVGL 的 DMA2D draw unit 接管 fill/blend/image。 */
#define LV_USE_DRAW_DMA2D           1
#define LV_DRAW_DMA2D_HAL_INCLUDE   "stm32h7xx_hal.h"
/* 当前工程仍是裸机模式，DMA2D draw unit 会以同步方式运行，因此无需开中断异步。 */
#define LV_USE_DRAW_DMA2D_INTERRUPT 0

/*=================
   FONT USAGE
 ==================*/

/* Montserrat fonts with various styles and bpp */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
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

/* 中文字体支持 (需要自行添加字体文件) */
#define LV_FONT_CUSTOM_DECLARE

/*===================
   TEXT SETTINGS
 *===================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"

#define LV_USE_ST_LTDC 1

/*=================
   WIDGET USAGE
 ==================*/

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

#define LV_USE_LOG 0
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_USE_USER_DATA 1

#define LV_BUILD_EXAMPLES 0

#define LV_USE_DEMO_WIDGETS    1
#define LV_USE_DEMO_BENCHMARK  0
#define LV_USE_DEMO_STRESS     0
#define LV_USE_DEMO_MUSIC      0

#define LV_USE_SYSMON   0
#if LV_USE_SYSMON
    #define LV_SYSMON_GET_IDLE lv_os_get_idle_percent
    #define LV_SYSMON_PROC_IDLE_AVAILABLE 0
    #define LV_USE_PERF_MONITOR 1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
        #define LV_USE_PERF_MONITOR_LOG_MODE 0
    #endif

    #define LV_USE_MEM_MONITOR 0
    #if LV_USE_MEM_MONITOR
        #define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
    #endif
#endif /*LV_USE_SYSMON*/

#endif /* LV_CONF_H */
