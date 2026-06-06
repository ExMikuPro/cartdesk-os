#ifndef SDRAM_LAYOUT_H
#define SDRAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * SDRAM fixed layout defined by Docs/SDRAM_Layout_Spec_v1.0.md
 * Physical range: 0xD0000000 ~ 0xD3FFFFFF (64 MiB)
 */

#define SDRAM_BASE_ADDR               0xD0000000UL
#define SDRAM_TOTAL_SIZE              0x04000000UL
#define SDRAM_END_ADDR                (SDRAM_BASE_ADDR + SDRAM_TOTAL_SIZE)

#define SDRAM_LAYER1_FB0_ADDR         0xD0000000UL
#define SDRAM_LAYER1_FB0_SIZE         0x00177000UL
#define SDRAM_LAYER1_FB1_ADDR         0xD0177000UL
#define SDRAM_LAYER1_FB1_SIZE         0x00177000UL
#define SDRAM_LAYER2_FB0_ADDR         0xD02EE000UL
#define SDRAM_LAYER2_FB0_SIZE         0x00177000UL

#define SDRAM_LVGL_HEAP_ADDR          0xD0465000UL
#define SDRAM_LVGL_HEAP_SIZE          0x01000000UL

#define SDRAM_DMA_POOL_ADDR           0xD1465000UL
#define SDRAM_DMA_POOL_SIZE           0x00400000UL

#define SDRAM_LAUNCHER_CACHE_ADDR     0xD1865000UL
#define SDRAM_LAUNCHER_CACHE_SIZE     0x00400000UL

#define SDRAM_APP_ARENA_ADDR          0xD1C65000UL
#define SDRAM_APP_ARENA_SIZE          0x0239B000UL

#define SDRAM_LAYER1_FB0_END          (SDRAM_LAYER1_FB0_ADDR + SDRAM_LAYER1_FB0_SIZE)
#define SDRAM_LAYER1_FB1_END          (SDRAM_LAYER1_FB1_ADDR + SDRAM_LAYER1_FB1_SIZE)
#define SDRAM_LAYER2_FB0_END          (SDRAM_LAYER2_FB0_ADDR + SDRAM_LAYER2_FB0_SIZE)
#define SDRAM_LVGL_HEAP_END           (SDRAM_LVGL_HEAP_ADDR + SDRAM_LVGL_HEAP_SIZE)
#define SDRAM_DMA_POOL_END            (SDRAM_DMA_POOL_ADDR + SDRAM_DMA_POOL_SIZE)
#define SDRAM_LAUNCHER_CACHE_END      (SDRAM_LAUNCHER_CACHE_ADDR + SDRAM_LAUNCHER_CACHE_SIZE)
#define SDRAM_APP_ARENA_END           (SDRAM_APP_ARENA_ADDR + SDRAM_APP_ARENA_SIZE)

#define SDRAM_FB_ALIGN                256UL
#define SDRAM_DMA_ALIGN               64UL
#define SDRAM_DEFAULT_ALIGN           32UL

static inline uintptr_t sdram_align_up_uintptr(uintptr_t value, size_t align)
{
    uintptr_t mask = (uintptr_t)align - 1u;
    return (value + mask) & ~mask;
}

#endif /* SDRAM_LAYOUT_H */
