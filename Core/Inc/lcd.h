#include "main.h"

#define MAX_LAYER_NUMBER       ((uint32_t)2)

#define LTDC_ACTIVE_LAYER	     ((uint32_t)1) /* Layer 1 */

// typedef struct
// {
//     uint32_t TextColor;
//     uint32_t BackColor;
//     sFONT    *pFont;
// }LCD_DrawPropTypeDef;

void LCD_Select(uint32_t LayerIndex);
uint32_t LCD_GetYSize();
uint32_t LCD_GetXSize();
void LCD_DisplayON();
void LCD_DisplayOFF();
void LCD_SetTransparency(uint32_t LayerIndex,uint8_t Transparency);
void LCD_SetLayerVisible(uint32_t LayerIndex,uint8_t Status);