#include "xhgc_memory_layout.h"

#include <stdio.h>

/* 对齐要求直接取统一布局常量，避免在本文件重复魔法数字 */
#define XHGC_MEM_ALIGN_FB      SDRAM_FB_ALIGN
#define XHGC_MEM_ALIGN_DMA     SDRAM_DMA_ALIGN
#define XHGC_MEM_ALIGN_DEFAULT SDRAM_DEFAULT_ALIGN

/*
 * 分区表按地址升序排列且首尾相接，必须与 Core/Inc/sdram_layout.h 完全一致。
 * base/end 为 end-exclusive。
 */
const XHGC_MemZoneDesc g_xhgc_mem_zones[XHGC_MEM_ZONE_COUNT] = {
    {
        XHGC_MEM_ZONE_LAYER0_FB,
        "LAYER0_FB",
        (uintptr_t)SDRAM_LAYER0_FB_BASE,
        (uint32_t)SDRAM_LAYER0_FB_SIZE,
        (uintptr_t)SDRAM_LAYER0_FB_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_LVGL_FB_A,
        "LVGL_FB_A",
        (uintptr_t)SDRAM_LVGL_FB_A_BASE,
        (uint32_t)SDRAM_LVGL_FB_A_SIZE,
        (uintptr_t)SDRAM_LVGL_FB_A_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_LVGL_FB_B,
        "LVGL_FB_B",
        (uintptr_t)SDRAM_LVGL_FB_B_BASE,
        (uint32_t)SDRAM_LVGL_FB_B_SIZE,
        (uintptr_t)SDRAM_LVGL_FB_B_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_FRAMEBUFFER
    },
    {
        XHGC_MEM_ZONE_LAUNCHER_STRIP,
        "LAUNCHER_STRIP",
        (uintptr_t)SDRAM_LAUNCHER_STRIP_BASE,
        (uint32_t)SDRAM_LAUNCHER_STRIP_ARENA_SIZE,
        (uintptr_t)SDRAM_LAUNCHER_STRIP_ARENA_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_FIXED |
        XHGC_MEM_ZONE_FLAG_LAUNCHER_STRIP
    },
    {
        XHGC_MEM_ZONE_SDRAM_LVGL_HEAP,
        "SDRAM_LVGL_HEAP",
        (uintptr_t)SDRAM_LVGL_HEAP_BASE,
        (uint32_t)SDRAM_LVGL_HEAP_SIZE,
        (uintptr_t)SDRAM_LVGL_HEAP_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_LVGL_HEAP
    },
    {
        XHGC_MEM_ZONE_DMA_POOL,
        "DMA_POOL",
        (uintptr_t)SDRAM_DMA_POOL_BASE,
        (uint32_t)SDRAM_DMA_POOL_SIZE,
        (uintptr_t)SDRAM_DMA_POOL_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_DMA
    },
    {
        XHGC_MEM_ZONE_LAUNCHER_CACHE,
        "LAUNCHER_CACHE",
        (uintptr_t)SDRAM_LAUNCHER_CACHE_BASE,
        (uint32_t)SDRAM_LAUNCHER_CACHE_SIZE,
        (uintptr_t)SDRAM_LAUNCHER_CACHE_END,
        XHGC_MEM_ZONE_FLAG_SDRAM |
        XHGC_MEM_ZONE_FLAG_CACHE
    },
    {
        XHGC_MEM_ZONE_APP_ARENA_REST,
        "APP_ARENA_REST",
        (uintptr_t)SDRAM_APP_ARENA_BASE,
        (uint32_t)SDRAM_APP_ARENA_SIZE,
        (uintptr_t)(SDRAM_APP_ARENA_END + 1UL),
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
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_LAUNCHER_STRIP, "LAUNCHER_STRIP");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_LVGL_HEAP, "LVGL_HEAP");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_DMA, "DMA");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_CACHE, "CACHE");
    XHGC_MEM_PRINT_FLAG(XHGC_MEM_ZONE_FLAG_ARENA, "ARENA");

#undef XHGC_MEM_PRINT_FLAG

    if (!printed) {
        printf("0");
    }
}

/**
 * @brief  按内存 zone ID 获取 SDRAM 分区描述
 * @param  id: 内存 zone ID
 * @return 非NULL=对应分区描述, NULL=ID非法
 */
const XHGC_MemZoneDesc* xhgc_mem_get_zone(XHGC_MemZoneId id)
{
    if ((int)id < 0 || id >= XHGC_MEM_ZONE_COUNT) {
        return NULL;
    }

    return &g_xhgc_mem_zones[id];
}

/**
 * @brief  查找包含指定地址的 SDRAM 分区
 * @param  addr: 待查询地址
 * @return 非NULL=包含该地址的分区描述, NULL=地址不在已登记分区内
 */
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

/**
 * @brief  判断地址范围是否完整落在指定 SDRAM 分区内
 * @param  id: 目标内存 zone ID
 * @param  addr: 起始地址
 * @param  size: 范围字节数
 * @retval true=范围完整位于分区内
 * @retval false=ID非法、长度为0、地址溢出或范围越界
 */
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

/**
 * @brief  判断地址范围是否属于允许 DMA 访问的固定 SDRAM 区域
 * @param  ptr: 起始地址
 * @param  size: 范围字节数
 * @retval true=范围位于固定 DMA 目标分区内
 * @retval false=参数非法或范围不属于固定 DMA 目标分区
 * @note   - 本函数只检查已列入白名单的固定分区，不包含 DMA_POOL 本身
 * @note   - 调用方仍需按具体 DMA 外设要求处理 cache clean / invalidate
 */
bool xhgc_mem_is_fixed_dma_target(const void *ptr, size_t size)
{
    static const XHGC_MemZoneId fixed_dma_zones[] = {
        XHGC_MEM_ZONE_LAYER0_FB,
        XHGC_MEM_ZONE_LVGL_FB_A,
        XHGC_MEM_ZONE_LVGL_FB_B,
        XHGC_MEM_ZONE_LAUNCHER_STRIP,
        XHGC_MEM_ZONE_LAUNCHER_CACHE,
        XHGC_MEM_ZONE_APP_ARENA_REST
    };
    uintptr_t addr = (uintptr_t)ptr;

    if (ptr == NULL || size == 0u || size > UINT32_MAX) {
        return false;
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(fixed_dma_zones) / sizeof(fixed_dma_zones[0])); ++i) {
        if (xhgc_mem_addr_in_zone(fixed_dma_zones[i], addr, (uint32_t)size)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief  校验 SDRAM 分区表与编译期布局常量是否一致
 * @retval true=布局连续、边界和对齐均符合预期
 * @retval false=基址、大小、连续性、越界或对齐检查失败
 * @note   本函数只读检查全局分区表，不修改初始化顺序或内存内容
 * @note   分区表的数值来源是 sdram_layout.h，本函数因此同时是统一布局的
 *         运行时回归检查
 */
bool xhgc_mem_layout_validate(void)
{
    uintptr_t expected_base = XHGC_SDRAM_BASE;

    if (XHGC_SDRAM_BASE != SDRAM_BASE_ADDR) {
        return false;
    }

    if (XHGC_SDRAM_SIZE != SDRAM_TOTAL_SIZE) {
        return false;
    }

    if (XHGC_SDRAM_END_EXCLUSIVE != SDRAM_LIMIT_ADDR) {
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

    if (g_xhgc_mem_zones[XHGC_MEM_ZONE_LAYER0_FB].end != SDRAM_LVGL_FB_A_BASE ||
        g_xhgc_mem_zones[XHGC_MEM_ZONE_LVGL_FB_A].end != SDRAM_LVGL_FB_B_BASE ||
        g_xhgc_mem_zones[XHGC_MEM_ZONE_LVGL_FB_B].end != SDRAM_LAUNCHER_STRIP_BASE ||
        g_xhgc_mem_zones[XHGC_MEM_ZONE_LAUNCHER_STRIP].end != SDRAM_LVGL_HEAP_BASE) {
        return false;
    }

    if (LAUNCHER_STRIP_ALLOC_SIZE > g_xhgc_mem_zones[XHGC_MEM_ZONE_LAUNCHER_STRIP].size) {
        return false;
    }

    return true;
}

/**
 * @brief  打印当前 SDRAM 分区表
 * @retval None
 * @note   本函数只输出分区基址、大小、结束地址和标志，不修改全局状态
 */
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
static_assert(XHGC_MEM_ZONE_COUNT == 8, "unexpected XHGC memory zone count");
#else
_Static_assert(XHGC_MEM_ZONE_COUNT == 8, "unexpected XHGC memory zone count");
#endif
