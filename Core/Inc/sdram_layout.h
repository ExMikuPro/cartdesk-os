#ifndef SDRAM_LAYOUT_H
#define SDRAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * SDRAM fixed layout defined by Docs/memory/SDRAM_Layout_Spec_v1.0.md
 * Physical range: 0xD0000000 ~ 0xD3FFFFFF (64 MiB)
 */

#define SDRAM_BASE_ADDR               ((uintptr_t)0xD0000000UL)
#define SDRAM_TOTAL_SIZE              ((uint32_t)0x04000000UL)
#define SDRAM_END_ADDR                ((uintptr_t)0xD3FFFFFFUL)
#define SDRAM_LIMIT_ADDR              (SDRAM_BASE_ADDR + (uintptr_t)SDRAM_TOTAL_SIZE)

#define SDRAM_LAYER1_FB0_BASE         ((uintptr_t)0xD0000000UL)
#define SDRAM_LAYER1_FB0_END          ((uintptr_t)0xD0176FFFUL)
#define SDRAM_LAYER1_FB0_SIZE         ((uint32_t)(SDRAM_LAYER1_FB0_END - SDRAM_LAYER1_FB0_BASE + 1UL))

#define SDRAM_LAYER1_FB1_BASE         ((uintptr_t)0xD0177000UL)
#define SDRAM_LAYER1_FB1_END          ((uintptr_t)0xD02EDFFFUL)
#define SDRAM_LAYER1_FB1_SIZE         ((uint32_t)(SDRAM_LAYER1_FB1_END - SDRAM_LAYER1_FB1_BASE + 1UL))

#define SDRAM_LAYER2_FB0_BASE         ((uintptr_t)0xD02EE000UL)
#define SDRAM_LAYER2_FB0_END          ((uintptr_t)0xD0464FFFUL)
#define SDRAM_LAYER2_FB0_SIZE         ((uint32_t)(SDRAM_LAYER2_FB0_END - SDRAM_LAYER2_FB0_BASE + 1UL))

#define SDRAM_LVGL_HEAP_BASE          ((uintptr_t)0xD0465000UL)
#define SDRAM_LVGL_HEAP_END           ((uintptr_t)0xD1464FFFUL)
#define SDRAM_LVGL_HEAP_SIZE          ((uint32_t)(SDRAM_LVGL_HEAP_END - SDRAM_LVGL_HEAP_BASE + 1UL))

#define SDRAM_DMA_POOL_BASE           ((uintptr_t)0xD1465000UL)
#define SDRAM_DMA_POOL_END            ((uintptr_t)0xD1864FFFUL)
#define SDRAM_DMA_POOL_SIZE           ((uint32_t)(SDRAM_DMA_POOL_END - SDRAM_DMA_POOL_BASE + 1UL))

#define SDRAM_LAUNCHER_CACHE_BASE     ((uintptr_t)0xD1865000UL)
#define SDRAM_LAUNCHER_CACHE_END      ((uintptr_t)0xD1C64FFFUL)
#define SDRAM_LAUNCHER_CACHE_SIZE     ((uint32_t)(SDRAM_LAUNCHER_CACHE_END - SDRAM_LAUNCHER_CACHE_BASE + 1UL))

#define SDRAM_APP_ARENA_BASE          ((uintptr_t)0xD1C65000UL)
#define SDRAM_APP_ARENA_END           ((uintptr_t)0xD3FFFFFFUL)
#define SDRAM_APP_ARENA_SIZE          ((uint32_t)(SDRAM_APP_ARENA_END - SDRAM_APP_ARENA_BASE + 1UL))

/* Backward-compatible names used by existing drivers. */
#define SDRAM_LAYER1_FB0_ADDR         SDRAM_LAYER1_FB0_BASE
#define SDRAM_LAYER1_FB1_ADDR         SDRAM_LAYER1_FB1_BASE
#define SDRAM_LAYER2_FB0_ADDR         SDRAM_LAYER2_FB0_BASE
#define SDRAM_LVGL_HEAP_ADDR          SDRAM_LVGL_HEAP_BASE
#define SDRAM_DMA_POOL_ADDR           SDRAM_DMA_POOL_BASE
#define SDRAM_LAUNCHER_CACHE_ADDR     SDRAM_LAUNCHER_CACHE_BASE
#define SDRAM_APP_ARENA_ADDR          SDRAM_APP_ARENA_BASE

#define SDRAM_FB_ALIGN                ((uint32_t)256UL)
#define SDRAM_DMA_ALIGN               ((uint32_t)64UL)
#define SDRAM_DEFAULT_ALIGN           ((uint32_t)32UL)

/*
 * APP_ARENA_REST split:
 *   - Lua heap owns the low 2 MiB and is never reset by resource arena APIs
 *   - resource arena grows upward immediately after the Lua heap
 *   - cold pool reserves the high 8 MiB for cold metadata/cache objects
 */
#define LUA_HEAP_SIZE                 ((uint32_t)0x00200000UL)
#define LUA_HEAP_BASE                 SDRAM_APP_ARENA_BASE
#define LUA_HEAP_END                  ((uintptr_t)(LUA_HEAP_BASE + (uintptr_t)LUA_HEAP_SIZE - 1UL))

#define COLD_POOL_SIZE                ((uint32_t)0x00800000UL)
#define COLD_POOL_END                 SDRAM_APP_ARENA_END
#define COLD_POOL_BASE                ((uintptr_t)(COLD_POOL_END + 1UL - (uintptr_t)COLD_POOL_SIZE))

#define RESOURCE_ARENA_BASE           ((uintptr_t)(LUA_HEAP_END + 1UL))
#define RESOURCE_ARENA_END            ((uintptr_t)(COLD_POOL_BASE - 1UL))
#define RESOURCE_ARENA_SIZE           ((uint32_t)(RESOURCE_ARENA_END - RESOURCE_ARENA_BASE + 1UL))

static inline uintptr_t sdram_align_up_uintptr(uintptr_t value, size_t align)
{
    uintptr_t mask = (uintptr_t)align - 1u;
    return (value + mask) & ~mask;
}

static inline int sdram_addr_in_range(uintptr_t addr, uintptr_t base, uintptr_t end)
{
    return (addr >= base) && (addr <= end);
}

static inline int sdram_addr_in_fb(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAYER1_FB0_BASE, SDRAM_LAYER1_FB0_END) ||
           sdram_addr_in_range(addr, SDRAM_LAYER1_FB1_BASE, SDRAM_LAYER1_FB1_END) ||
           sdram_addr_in_range(addr, SDRAM_LAYER2_FB0_BASE, SDRAM_LAYER2_FB0_END);
}

static inline int sdram_addr_in_lvgl_heap(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END);
}

static inline int sdram_addr_in_dma_pool(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END);
}

static inline int sdram_addr_in_launcher_cache(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END);
}

static inline int sdram_addr_in_app_arena(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END);
}

static inline int sdram_addr_in_lua_heap(uintptr_t addr)
{
    return sdram_addr_in_range(addr, LUA_HEAP_BASE, LUA_HEAP_END);
}

static inline int sdram_addr_in_resource_arena(uintptr_t addr)
{
    return sdram_addr_in_range(addr, RESOURCE_ARENA_BASE, RESOURCE_ARENA_END);
}

static inline int sdram_addr_in_cold_pool(uintptr_t addr)
{
    return sdram_addr_in_range(addr, COLD_POOL_BASE, COLD_POOL_END);
}

#ifdef __cplusplus
#define SDRAM_STATIC_ASSERT static_assert
#else
#define SDRAM_STATIC_ASSERT _Static_assert
#endif

SDRAM_STATIC_ASSERT(SDRAM_LIMIT_ADDR == (SDRAM_END_ADDR + 1UL), "SDRAM limit mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAYER1_FB0_SIZE == 0x00177000UL, "Layer1_FB0 size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAYER1_FB1_SIZE == 0x00177000UL, "Layer1_FB1 size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAYER2_FB0_SIZE == 0x00177000UL, "Layer2_FB0 size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_HEAP_SIZE == 0x01000000UL, "LVGL heap size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_DMA_POOL_SIZE == 0x00400000UL, "DMA pool size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_CACHE_SIZE == 0x00400000UL, "Launcher cache size mismatch");
SDRAM_STATIC_ASSERT(LUA_HEAP_BASE == 0xD1C65000UL, "Lua heap base mismatch");
SDRAM_STATIC_ASSERT(LUA_HEAP_SIZE == 0x00200000UL, "Lua heap size mismatch");
SDRAM_STATIC_ASSERT(RESOURCE_ARENA_BASE == 0xD1E65000UL, "Resource arena base mismatch");
SDRAM_STATIC_ASSERT(LUA_HEAP_END < RESOURCE_ARENA_BASE, "Lua heap must not overlap resource arena");
SDRAM_STATIC_ASSERT(COLD_POOL_BASE > RESOURCE_ARENA_BASE, "Cold pool must be above resource arena");

#undef SDRAM_STATIC_ASSERT

#endif /* SDRAM_LAYOUT_H */
