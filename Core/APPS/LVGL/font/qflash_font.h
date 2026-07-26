#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QFLASH_FONT_PACK_OFFSET      0x00000000u
#define QFLASH_FONT_REGION_SIZE      0x01000000u
#define QFLASH_FONT_DEFAULT_SIZE     20u

extern lv_font_t qflash_font_16;
extern lv_font_t qflash_font_20;
extern lv_font_t qflash_font_24;

/**
 * @brief Mount a memory-mapped QFNT font pack.
 * @param mapped_base QFLASH memory-mapped address including pack offset.
 * @param region_size Maximum readable bytes beginning at mapped_base.
 */
bool QFlashFont_Mount(const void *mapped_base, size_t region_size);

/**
 * @brief Return a mounted QFLASH font by pixel size.
 * @return Font pointer, or NULL when the requested size is unavailable.
 */
const lv_font_t *QFlashFont_Get(uint16_t pixel_size);

bool QFlashFont_IsMounted(void);
const char *QFlashFont_LastError(void);

#ifdef __cplusplus
}
#endif
