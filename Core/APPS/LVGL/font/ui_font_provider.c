#include "ui_font_provider.h"

#include "qflash_font.h"

const lv_font_t *UiFont_GetSystem(uint16_t pixel_size)
{
    const lv_font_t *font = QFlashFont_Get(pixel_size);
    if (font != NULL) {
        return font;
    }

    if (pixel_size == 16u) {
        return &lv_font_montserrat_16;
    }
    return &lv_font_montserrat_20;
}
