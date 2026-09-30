#ifndef LTDC_LAYER_H
#define LTDC_LAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LTDC 硬件图层配置（Launcher 双图层 / Lua 全屏模式）。
 *
 * ============================ 冻结的真实图层语义 ============================
 *
 *   HW Layer0  ==  HAL LayerIdx 0  ==  LTDC_Layer1 寄存器块  ==  偏移 0x84
 *   HW Layer1  ==  HAL LayerIdx 1  ==  LTDC_Layer2 寄存器块  ==  偏移 0x104
 *
 * 注意 stm32h7xx_hal_ltdc.h 的 LTDC_LAYER(hltdc, idx) 宏：
 *   base + 0x84 + 0x80 * idx
 * 所以 HAL 的 "LTDC_Layer1" 名字对应的是 **硬件 Layer0**。本仓库不再使用
 * Layer1_FB0 / Layer2_FB0 之类含糊命名表达硬件层，统一使用：
 *
 *   HW_L0_STATIC   —— Launcher 固定 UI（单缓冲，低频更新）
 *   HW_L1_CONTENT  —— Launcher strip（硬件平移）或 LVGL DIRECT 双缓冲
 *
 * ============================== 寄存器编码语义 ==============================
 *
 * LTDC_SetConfig() 的 CFBLR 编码（已核对 stm32h7xx_hal_ltdc.c:2189-2194）：
 *
 *   CFBLR.CFBP  (bits 28:16) = ImageWidth * bytes_per_pixel
 *   CFBLR.CFBLL (bits 15:0)  = (WindowX1 - WindowX0) * bytes_per_pixel + 7
 *   CFBLNR                   = ImageHeight
 *
 * 也就是说 HAL 的 ImageWidth 字段表达的是 **pitch（像素）**，不是可见宽度。
 * 因此"可见宽度 800、pitch 10656 B"可以用公开 HAL API 表达，不需要散写
 * 寄存器：
 *
 *   WindowX0 = 0, WindowX1 = 800, ImageWidth = 10656/4 = 2664
 *   => CFBP = 10656 (0x29A0), CFBLL = 3200 + 7 = 3207 (0xC87)
 *
 * 所有写入都是 NoReload 变体（只改 shadow，不写 SRCR.IMR），由调用方在
 * 临界区内 arm reload owner 后再发一次 SRCR.VBR。
 */

#define HW_L0_STATIC_LAYER_IDX   0u
#define HW_L1_CONTENT_LAYER_IDX  1u

/**
 * @brief  把 HW Layer1 配置成 Launcher strip 窗口（硬件平移）
 * @param  source_addr    strip 的当前 source 起始地址（base + scroll_x*4）
 * @param  window_y       窗口在面板上的起始行
 * @param  window_height  窗口高度（同时作为 CFBLNR 的行数）
 * @param  pitch_pixels   物理 pitch（像素）＝ stride_bytes / 4
 * @param  viewport_width 可见宽度（像素）
 * @retval true=配置已写入 shadow
 * @note   只写 shadow，不请求 VBR
 */
bool Lcd_LtdcConfigureStripWindow(uintptr_t source_addr,
                                  uint32_t window_y,
                                  uint32_t window_height,
                                  uint32_t pitch_pixels,
                                  uint32_t viewport_width);

/**
 * @brief  把 HW Layer1 配置回 800x480 全屏 LVGL DIRECT 模式
 * @param  fb_addr 当前 LVGL framebuffer 地址
 * @retval true=配置已写入 shadow
 */
bool Lcd_LtdcConfigureLvglFullscreen(uintptr_t fb_addr);

/**
 * @brief  配置 HW Layer0 为全屏静态 framebuffer（Launcher 固定 UI）
 * @param  fb_addr 静态 framebuffer 地址
 * @retval true=配置已写入 shadow
 */
bool Lcd_LtdcConfigureLayer0Static(uintptr_t fb_addr);

/**
 * @brief  使能/停用某个硬件图层（写 CR.LEN，属 LTDC shadow，随 VBR 生效）
 * @param  layer_idx HW_L0_STATIC_LAYER_IDX / HW_L1_CONTENT_LAYER_IDX
 */
void Lcd_LtdcSetLayerEnabled(uint32_t layer_idx, bool enabled);

/** 只改 CFBAR（NoReload）；供 steady-state pan 使用 */
bool Lcd_LtdcSetSourceAddr(uint32_t layer_idx, uintptr_t addr);

/** 请求一次 VBlank reload；返回 true 表示 SRCR.VBR 已受理 */
bool Lcd_LtdcRequestVblankReload(void);

/** 读回硬件寄存器快照（审计/调试用） */
typedef struct {
    uint32_t layer0_cr;
    uint32_t layer0_cfbar;
    uint32_t layer0_cfblr;
    uint32_t layer0_cfblnr;
    uint32_t layer0_whpcr;
    uint32_t layer0_wvpcr;
    uint32_t layer1_cr;
    uint32_t layer1_cfbar;
    uint32_t layer1_cfblr;
    uint32_t layer1_cfblnr;
    uint32_t layer1_whpcr;
    uint32_t layer1_wvpcr;
    uint32_t srcr;
    uint32_t isr;
} Lcd_LtdcRegisterSnapshot;

void Lcd_LtdcSnapshotRegisters(Lcd_LtdcRegisterSnapshot *out);

/** Launcher strip 窗口当前是否与设计一致（偏移 0x104 上的 HW Layer1） */
bool Lcd_LtdcStripWindowMatches(uintptr_t expected_source_addr,
                                uint32_t expected_pitch_pixels,
                                uint32_t expected_viewport_width,
                                uint32_t expected_window_height);

/** 当前各层窗口参数（trace/dump 用） */
uint32_t Lcd_LtdcLayer0WindowY(void);
uint32_t Lcd_LtdcLayer0WindowH(void);
uint32_t Lcd_LtdcLayer1WindowY(void);
uint32_t Lcd_LtdcLayer1WindowH(void);

#ifdef __cplusplus
}
#endif

#endif /* LTDC_LAYER_H */
