#pragma once

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FONT_SYSTEM_DEFAULT_SIZE 20u

/**
 * @brief Return the shared system font for a supported pixel size.
 *
 * The mounted QFLASH font is preferred. If QFLASH is unavailable or the
 * requested size is absent, a built-in Montserrat font is returned.
 */
const lv_font_t *UiFont_GetSystem(uint16_t pixel_size);

#ifdef __cplusplus
}
#endif
