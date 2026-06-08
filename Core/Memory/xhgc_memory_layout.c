#include "xhgc_memory_layout.h"

#include <stdio.h>

#define XHGC_MEM_ALIGN_FB      256u
#define XHGC_MEM_ALIGN_DMA     64u
#define XHGC_MEM_ALIGN_DEFAULT 32u

const XHGC_MemZoneDesc g_xhgc_mem_zones[XHGC_MEM_ZONE_COUNT] = {
    {
        XHGC_MEM_ZONE_LAYER1_FB0,
        "Layer1_FB0",
        (uintptr_t)0xD0000000UL,
        (uint32_t)0x00177000UL,
        (uintptr_t)0xD0177000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_LAYER1_FB1,
        "Layer1_FB1",
        (uintptr_t)0xD0177000UL,
        (uint32_t)0x00177000UL,
        (uintptr_t)0xD02EE000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_LAYER2_FB0,
        "Layer2_FB0",
        (uintptr_t)0xD02EE000UL,
        (uint32_t)0x00177000UL,
        (uintptr_t)0xD0465000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_SDRAM_LVGL_HEAP,
        "SDRAM_LVGL_HEAP",
        (uintptr_t)0xD0465000UL,
        (uint32_t)0x01000000UL,
        (uintptr_t)0xD1465000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_LVGL_HEAP
    },
    {
        XHGC_MEM_ZONE_DMA_POOL,
        "DMA_POOL",
        (uintptr_t)0xD1465000UL,
        (uint32_t)0x00400000UL,
        (uintptr_t)0xD1865000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_DMA
    },
    {
        XHGC_MEM_ZONE_LAUNCHER_CACHE,
        "LAUNCHER_CACHE",
        (uintptr_t)0xD1865000UL,
        (uint32_t)0x00400000UL,
        (uintptr_t)0xD1C65000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_CACHE
    },
    {
        XHGC_MEM_ZONE_APP_ARENA_REST,
        "APP_ARENA_REST",
        (uintptr_t)0xD1C65000UL,
        (uint32_t)0x0239B000UL,
        (uintptr_t)0xD4000000UL,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_ARENA
    }
};

static bool xhgc_mem_is_aligned(uintptr_t value, uint32_t align)
{
    return (align != 0u) && ((value & ((uintptr_t)align - 1u)) == 0u);
}

static bool xhgc_mem_zone_is_aligned(const XHGC_MemZoneDesc *zone,
                                     uint32_t align)
{
    return xhgc_mem_is_aligned(zone->base, align) &&
           xhgc_mem_is_aligned(zone->end, align);
}

static void xhgc_mem_print_flags(uint32_t flags)
{
    bool printed = false;

#define XHGC_MEM_PRINT_FLAG(flag, text)           \
    do {                                          \
        if ((flags & (flag)) != 0u) {             \
            printf("%s%s", printed ? "|" : "", text); \
            printed = true;                       \
        }                                         \
    } while (0)

    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_SDRAM, "SDRAM");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_FIXED, "FIXED");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_FRAMEBUFFER, "FRAMEBUFFER");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_LVGL_HEAP, "LVGL_HEAP");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_DMA, "DMA");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_CACHE, "CACHE");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_ARENA, "ARENA");

#undef XHGC_MEM_PRINT_FLAG

    if (!printed) {
        printf("0");
    }
}

const XHGC_MemZoneDesc* xhgc_mem_get_zone(XHGC_MemZoneId id)
{
    if ((int)id < 0 || id >= XHGC_MEM_ZONE_COUNT) {
        return NULL;
    }

    return &g_xhgc_mem_zones[id];
}

const XHGC_MemZoneDesc* xhgc_mem_find_zone_by_addr(uintptr_t addr)
{
    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        const XHGC_MemZoneDesc *zone = &g_xhgc_mem_zones[i];
        if (addr >= zone->base && addr < zone->end) {
            return zone;
        }
    }

    return NULL;
}

bool xhgc_mem_addr_in_zone(XHGC_MemZoneId id, uintptr_t addr, uint32_t size)
{
    const XHGC_MemZoneDesc *zone;
    uintptr_t end;

    if (size == 0u) {
        return false;
    }

    zone = xhgc_mem_get_zone(id);
    if (zone == NULL) {
        return false;
    }

    end = addr + (uintptr_t)size;
    if (end < addr) {
        return false;
    }

    return addr >= zone->base && end <= zone->end;
}

bool xhgc_mem_layout_validate(void)
{
    uintptr_t expected_base = XHGC_SDRAM_BASE;

    if (XHGC_SDRAM_BASE != (uintptr_t)0xD0000000UL) {
        return false;
    }

    if (XHGC_SDRAM_SIZE != (uint32_t)0x04000000UL) {
        return false;
    }

    if (XHGC_SDRAM_END_EXCLUSIVE != (uintptr_t)0xD4000000UL) {
        return false;
    }

    if ((uintptr_t)(XHGC_SDRAM_BASE + (uintptr_t)XHGC_SDRAM_SIZE) !=
        XHGC_SDRAM_END_EXCLUSIVE) {
        return false;
    }

    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        const XHGC_MemZoneDesc *zone = &g_xhgc_mem_zones[i];
        uint32_t required_align = XHGC_MEM_ALIGN_DEFAULT;

        if (zone->id != (XHGC_MemZoneId)i) {
            return false;
        }

        if (zone->base < XHGC_SDRAM_BASE ||
            zone->end > XHGC_SDRAM_END_EXCLUSIVE ||
            zone->base >= zone->end) {
            return false;
        }

        if ((uintptr_t)(zone->base + (uintptr_t)zone->size) != zone->end) {
            return false;
        }

        if (zone->base != expected_base) {
            return false;
        }

        if (i > 0u && zone->base < g_xhgc_mem_zones[i - 1u].end) {
            return false;
        }

        if ((zone->flags & XHGC_MEM_ZONE_FLAG_FRAMEBUFFER) != 0u) {
            required_align = XHGC_MEM_ALIGN_FB;
        } else if (zone->id == XHGC_MEM_ZONE_DMA_POOL) {
            required_align = XHGC_MEM_ALIGN_DMA;
        }

        if (!xhgc_mem_zone_is_aligned(zone, required_align)) {
            return false;
        }

        expected_base = zone->end;
    }

    if (g_xhgc_mem_zones[XHGC_MEM_ZONE_COUNT - 1].end !=
        XHGC_SDRAM_END_EXCLUSIVE) {
        return false;
    }

    if (g_xhgc_mem_zones[XHGC_MEM_ZONE_APP_ARENA_REST].end !=
        XHGC_SDRAM_END_EXCLUSIVE) {
        return false;
    }

    return true;
}

void xhgc_mem_layout_dump(void)
{
    printf("[XHGC SDRAM LAYOUT]\r\n");

    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        const XHGC_MemZoneDesc *zone = &g_xhgc_mem_zones[i];

        printf("%-16s base=0x%08lX size=0x%08lX end=0x%08lX flags=",
               zone->name,
               (unsigned long)zone->base,
               (unsigned long)zone->size,
               (unsigned long)zone->end);
        xhgc_mem_print_flags(zone->flags);
        printf("\r\n");
    }
}

#if defined(__cplusplus)
static_assert(XHGC_MEM_ZONE_COUNT == 7, "unexpected XHGC memory zone count");
#else
_Static_assert(XHGC_MEM_ZONE_COUNT == 7, "unexpected XHGC memory zone count");
#endif
