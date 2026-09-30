#include "ltdc_layer.h"

#include <string.h>

#include "lcd.h"

/*
 * 真实寄存器块地址由 HAL 宏计算：base + 0x84 + 0x80 * idx。
 * 这里直接复用 HAL 的 LTDC_LAYER() 宏，避免再散落偏移常量。
 */
#define HW_L0_REG   LTDC_LAYER(&hltdc, HW_L0_STATIC_LAYER_IDX)
#define HW_L1_REG   LTDC_LAYER(&hltdc, HW_L1_CONTENT_LAYER_IDX)

#define LTDC_BYTES_PER_PIXEL_ARGB8888 4u

extern LTDC_HandleTypeDef hltdc;

static uint32_t s_layer0_window_y;
static uint32_t s_layer0_window_h;
static uint32_t s_layer1_window_y;
static uint32_t s_layer1_window_h;
static uint32_t s_layer1_pitch_pixels;
static uint32_t s_layer1_viewport_width;

static void fill_common(LTDC_LayerCfgTypeDef *cfg,
                        uint32_t window_x1,
                        uint32_t window_y0,
                        uint32_t window_y1,
                        uint32_t pitch_pixels,
                        uintptr_t fb_addr)
{
    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->WindowX0 = 0u;
    cfg->WindowX1 = window_x1;
    cfg->WindowY0 = window_y0;
    cfg->WindowY1 = window_y1;
    cfg->PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
    cfg->Alpha = 255u;
    cfg->Alpha0 = 0u;
    cfg->BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    cfg->BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    cfg->FBStartAdress = (uint32_t)fb_addr;
    /* HAL 语义：ImageWidth 字段 = pitch（像素），ImageHeight = 行数(CFBLNR) */
    cfg->ImageWidth = pitch_pixels;
    cfg->ImageHeight = window_y1 - window_y0;
    cfg->Backcolor.Blue = 0u;
    cfg->Backcolor.Green = 0u;
    cfg->Backcolor.Red = 0u;
}

bool Lcd_LtdcConfigureStripWindow(uintptr_t source_addr,
                                  uint32_t window_y,
                                  uint32_t window_height,
                                  uint32_t pitch_pixels,
                                  uint32_t viewport_width)
{
    if (viewport_width == 0u || window_height == 0u || pitch_pixels < viewport_width) {
        return false;
    }

    LTDC_LayerCfgTypeDef cfg;
    fill_common(&cfg,
                viewport_width,                 /* WindowX1 = 800 -> CFBLL = 3200 + 7 */
                window_y,
                window_y + window_height,        /* WindowY1 为 exclusive */
                pitch_pixels,                    /* CFBP = 10656 */
                source_addr);

    if (HAL_LTDC_ConfigLayer_NoReload(&hltdc, &cfg, HW_L1_CONTENT_LAYER_IDX) != HAL_OK) {
        return false;
    }

    s_layer1_window_y = window_y;
    s_layer1_window_h = window_height;
    s_layer1_pitch_pixels = pitch_pixels;
    s_layer1_viewport_width = viewport_width;
    return true;
}

bool Lcd_LtdcConfigureLvglFullscreen(uintptr_t fb_addr)
{
    LTDC_LayerCfgTypeDef cfg;
    fill_common(&cfg,
                LCD_W,
                0u,
                LCD_H,
                LCD_W,                           /* pitch = 800 px = 3200 B */
                fb_addr);

    if (HAL_LTDC_ConfigLayer_NoReload(&hltdc, &cfg, HW_L1_CONTENT_LAYER_IDX) != HAL_OK) {
        return false;
    }

    s_layer1_window_y = 0u;
    s_layer1_window_h = LCD_H;
    s_layer1_pitch_pixels = LCD_W;
    s_layer1_viewport_width = LCD_W;
    return true;
}

bool Lcd_LtdcConfigureLayer0Static(uintptr_t fb_addr)
{
    LTDC_LayerCfgTypeDef cfg;
    fill_common(&cfg,
                LCD_W,
                0u,
                LCD_H,
                LCD_W,
                fb_addr);

    if (HAL_LTDC_ConfigLayer_NoReload(&hltdc, &cfg, HW_L0_STATIC_LAYER_IDX) != HAL_OK) {
        return false;
    }

    s_layer0_window_y = 0u;
    s_layer0_window_h = LCD_H;
    return true;
}

void Lcd_LtdcSetLayerEnabled(uint32_t layer_idx, bool enabled)
{
    if (enabled) {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, layer_idx);
    } else {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, layer_idx);
    }
}

bool Lcd_LtdcSetSourceAddr(uint32_t layer_idx, uintptr_t addr)
{
    /* NoReload：只改 shadow CFBAR，不写 SRCR.IMR，由调用方决定何时 VBR。 */
    return HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)addr, layer_idx) == HAL_OK;
}

bool Lcd_LtdcRequestVblankReload(void)
{
    return HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) == HAL_OK;
}

void Lcd_LtdcSnapshotRegisters(Lcd_LtdcRegisterSnapshot *out)
{
    if (out == NULL) {
        return;
    }

    out->layer0_cr      = HW_L0_REG->CR;
    out->layer0_cfbar   = HW_L0_REG->CFBAR;
    out->layer0_cfblr   = HW_L0_REG->CFBLR;
    out->layer0_cfblnr  = HW_L0_REG->CFBLNR;
    out->layer0_whpcr   = HW_L0_REG->WHPCR;
    out->layer0_wvpcr   = HW_L0_REG->WVPCR;
    out->layer1_cr      = HW_L1_REG->CR;
    out->layer1_cfbar   = HW_L1_REG->CFBAR;
    out->layer1_cfblr   = HW_L1_REG->CFBLR;
    out->layer1_cfblnr  = HW_L1_REG->CFBLNR;
    out->layer1_whpcr   = HW_L1_REG->WHPCR;
    out->layer1_wvpcr   = HW_L1_REG->WVPCR;
    out->srcr           = hltdc.Instance->SRCR;
    out->isr            = hltdc.Instance->ISR;
}

bool Lcd_LtdcStripWindowMatches(uintptr_t expected_source_addr,
                                uint32_t expected_pitch_pixels,
                                uint32_t expected_viewport_width,
                                uint32_t expected_window_height)
{
    const uint32_t expected_cfblr =
        ((expected_pitch_pixels * LTDC_BYTES_PER_PIXEL_ARGB8888) << 16U) |
        ((expected_viewport_width * LTDC_BYTES_PER_PIXEL_ARGB8888) + 7U);

    return HW_L1_REG->CFBAR == (uint32_t)expected_source_addr &&
           HW_L1_REG->CFBLR == expected_cfblr &&
           HW_L1_REG->CFBLNR == expected_window_height &&
           s_layer1_pitch_pixels == expected_pitch_pixels &&
           s_layer1_viewport_width == expected_viewport_width &&
           s_layer1_window_h == expected_window_height;
}

/* 供 trace/dump 读取当前窗口状态（未使用参数保持接口稳定） */
uint32_t Lcd_LtdcLayer0WindowY(void) { return s_layer0_window_y; }
uint32_t Lcd_LtdcLayer0WindowH(void) { return s_layer0_window_h; }
uint32_t Lcd_LtdcLayer1WindowY(void) { return s_layer1_window_y; }
uint32_t Lcd_LtdcLayer1WindowH(void) { return s_layer1_window_h; }
