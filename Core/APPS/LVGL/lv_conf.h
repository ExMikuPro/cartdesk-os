/**
 * @file lv_conf.h
 * @brief LVGL 9.4 配置文件 - 针对STM32H743优化
 *
 * 配置说明:
 * - MCU: STM32H743 (Cortex-M7)
 * - RAM: 1MB SRAM + 8MB SDRAM
 * - 显示: 800x480 ARGB8888
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COMPILER SETTINGS
 *====================*/

/* Cortex-M7：内存池/绘制缓冲 32B 对齐，避免 UNALIGNED UsageFault，也更利于 DMA2D/DCache */
#ifndef LV_ATTRIBUTE_MEM_ALIGN_SIZE
#  define LV_ATTRIBUTE_MEM_ALIGN_SIZE 32
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#  define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))
#endif

#ifndef LV_DRAW_BUF_ALIGN
#  define LV_DRAW_BUF_ALIGN 32
#endif

#ifndef LV_DRAW_BUF_STRIDE_ALIGN
#  define LV_DRAW_BUF_STRIDE_ALIGN 32
#endif

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 32

/*=========================
   MEMORY SETTINGS
 *=========================*/

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*
 * 你的 SDRAM 实际按 8MB 在用：0xD0000000 ~ 0xD07FFFFF
 *
 * 已占用布局（你工程里是这样）：
 *   Layer0 FB       0xD0000000 ~ 0xD0176FFF (0x177000)
 *   Layer1 FB0      0xD0177000 ~ 0xD02EDFFF (0x177000)
 *   Layer1 FB1      0xD02EE000 ~ 0xD0464FFF (0x177000)
 *   Image slots     0xD0465000 ~ 0xD0752FFF (12 * 0x3E800 = 0x2EE000)
 *
 * 所以 LVGL heap 只能放在剩余尾部：0xD0753000 ~ 0xD07FFFFF
 * 这里对齐到 0xD0754000，给 LVGL 640KB，尾部留余量防冲突。
 */
#define LV_MEM_ADR  0xD0754000U
#define LV_MEM_SIZE (640U * 1024U)   /* 640KB */

/*
 * 注意：你现在 SDRAM 剩余空间不到 700KB，
 * 所以图片缓存不能再设 4MB，否则必崩。
 * 先给一个“能明显减少抖动，但不爆内存”的尺寸：128KB。
 * 如果你确定不用 LVGL 图片缓存（你的图已预拷到 SDRAM 固定槽），也可以设 0。
 */
#define LV_CACHE_DEF_SIZE             (128U * 1024U)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 16

#define LV_USE_RLE 1
#define LV_BIN_DECODER_RAM_LOAD 1

#define LV_FONT_DEFAULT &lv_font_montserrat_20

/*=========================
   HAL SETTINGS
 *=========================*/
#define LV_DEF_REFR_PERIOD  10
#define LV_INDEV_DEF_READ_PERIOD 10

/*=======================
   OPERATING SYSTEM
 *=======================*/
#define LV_USE_OS LV_OS_NONE

/*========================
   RENDERING CONFIGURATION
 *========================*/
#define LV_DISPLAY_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_ASM               LV_DRAW_SW_ASM_NONE
    #define LV_DRAW_SW_COMPLEX           1
    #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
    #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #define LV_DRAW_SW_GRADIENT_CACHE_SIZE 1
#endif

#define LV_USE_DRAW_DMA2D           0
#define LV_DRAW_DMA2D_HAL_INCLUDE   "stm32h7xx_hal.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT 0

/*=================
   FONT USAGE
 ==================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1

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
#define LV_USE_IMAGE 1
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_SLIDER 1
#define LV_USE_LIST 1
#define LV_USE_MENU 1
#define LV_USE_TABVIEW 1
#define LV_USE_WIN 1
/* 你原文件里一堆控件开关可继续保留，这里省略不影响核心问题 */

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

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_USE_USER_DATA 1

#define LV_BUILD_EXAMPLES 0

#endif /* LV_CONF_H */
