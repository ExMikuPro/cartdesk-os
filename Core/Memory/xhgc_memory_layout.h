#ifndef XHGC_MEMORY_LAYOUT_H
#define XHGC_MEMORY_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XHGC_SDRAM_BASE          ((uintptr_t)0xD0000000UL)
#define XHGC_SDRAM_SIZE          ((uint32_t)0x04000000UL)
#define XHGC_SDRAM_END_EXCLUSIVE ((uintptr_t)0xD4000000UL)

typedef enum {
    XHGC_MEM_ZONE_LAYER1_FB0 = 0,
    XHGC_MEM_ZONE_LAYER1_FB1,
    XHGC_MEM_ZONE_LAYER2_FB0,
    XHGC_MEM_ZONE_SDRAM_LVGL_HEAP,
    XHGC_MEM_ZONE_DMA_POOL,
    XHGC_MEM_ZONE_LAUNCHER_CACHE,
    XHGC_MEM_ZONE_APP_ARENA_REST,
    XHGC_MEM_ZONE_COUNT
} XHGC_MemZoneId;

typedef enum {
    XHGC_MEM_ZONE_FLAG_SDRAM       = (1u << 0),
    XHGC_MEM_ZONE_FLAG_FIXED       = (1u << 1),
    XHGC_MEM_ZONE_FLAG_FRAMEBUFFER = (1u << 2),
    XHGC_MEM_ZONE_FLAG_LVGL_HEAP   = (1u << 3),
    XHGC_MEM_ZONE_FLAG_DMA         = (1u << 4),
    XHGC_MEM_ZONE_FLAG_CACHE       = (1u << 5),
    XHGC_MEM_ZONE_FLAG_ARENA       = (1u << 6)
} XHGC_MemZoneFlags;

typedef struct {
    XHGC_MemZoneId id;
    const char *name;
    uintptr_t base;
    uint32_t size;
    uintptr_t end;
    uint32_t flags;
} XHGC_MemZoneDesc;

extern const XHGC_MemZoneDesc g_xhgc_mem_zones[XHGC_MEM_ZONE_COUNT];

const XHGC_MemZoneDesc* xhgc_mem_get_zone(XHGC_MemZoneId id);
const XHGC_MemZoneDesc* xhgc_mem_find_zone_by_addr(uintptr_t addr);
bool xhgc_mem_addr_in_zone(XHGC_MemZoneId id, uintptr_t addr, uint32_t size);
bool xhgc_mem_layout_validate(void);
void xhgc_mem_layout_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_MEMORY_LAYOUT_H */
