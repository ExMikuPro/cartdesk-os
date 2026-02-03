#include "lcd.h"

#include "ltdc.h"

static uint32_t ActiveLayer = 0;

// 选择层
void LCD_Select(uint32_t LayerIndex) {
    ActiveLayer = LayerIndex;
}

// 获取层的x长度
uint32_t LCD_GetXSize() {
    return hltdc.LayerCfg[ActiveLayer].ImageWidth;
}

// 获取层的y长度
uint32_t LCD_GetYSize() {
    return hltdc.LayerCfg[ActiveLayer].ImageHeight;
}

void LCD_DisplayON() {
    __HAL_LTDC_ENABLE(&hltdc);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

void LCD_DisplayOFF() {
    __HAL_LTDC_DISABLE(&hltdc);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
}

void LCD_SetTransparency(uint32_t LayerIndex,uint8_t Transparency) {
    HAL_LTDC_SetAlpha(&hltdc,LayerIndex,Transparency);

}

void LCD_SetLayerVisible(uint32_t LayerIndex,uint8_t Status) {
    if (Status) {
        __HAL_LTDC_LAYER_ENABLE(&hltdc,LayerIndex);
    } else {
        __HAL_LTDC_LAYER_DISABLE(&hltdc,LayerIndex);
    }
}

