#include "xhgc_dcache.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "xhgc_memory_layout.h"

#define XHGC_DCACHE_LINE_SIZE 32u

typedef void (*xhgc_dcache_op_t)(uint32_t *addr, int32_t size);

static bool xhgc_range_align_32(const void *ptr, size_t size, uintptr_t *aligned_start, size_t *aligned_size)
{
    uintptr_t start;
    uintptr_t end;
    uintptr_t aligned_end;

    if (ptr == NULL || size == 0u) {
        return false;
    }

    start = (uintptr_t)ptr;
    if (size > (SIZE_MAX - start)) {
        return false;
    }

    end = start + size;
    if (end > (UINTPTR_MAX - (XHGC_DCACHE_LINE_SIZE - 1u))) {
        return false;
    }

    *aligned_start = start & ~(uintptr_t)(XHGC_DCACHE_LINE_SIZE - 1u);
    aligned_end = (end + (XHGC_DCACHE_LINE_SIZE - 1u)) &
                  ~(uintptr_t)(XHGC_DCACHE_LINE_SIZE - 1u);
    *aligned_size = (size_t)(aligned_end - *aligned_start);
    return *aligned_size <= (size_t)INT32_MAX;
}

#if defined(DEBUG)
static bool xhgc_range_in_zone(XHGC_MemZoneId zone_id, const void *ptr, size_t size)
{
    if (size > UINT32_MAX) {
        return false;
    }

    return xhgc_mem_addr_in_zone(zone_id, (uintptr_t)ptr, (uint32_t)size);
}

static bool xhgc_range_in_sdram(const void *ptr, size_t size)
{
    uintptr_t start;
    uintptr_t end;

    if (ptr == NULL || size == 0u || size > (SIZE_MAX - (uintptr_t)ptr)) {
        return false;
    }

    start = (uintptr_t)ptr;
    end = start + size;
    return start >= XHGC_SDRAM_BASE && end <= XHGC_SDRAM_END_EXCLUSIVE;
}

static void xhgc_dcache_debug_check(const void *ptr, size_t size)
{
    if (!xhgc_range_in_sdram(ptr, size)) {
        printf("[XHGC DCACHE] warning: range outside SDRAM ptr=0x%08lX size=0x%08lX\r\n",
               (unsigned long)(uintptr_t)ptr,
               (unsigned long)size);
    }

    if (!xhgc_range_in_zone(XHGC_MEM_ZONE_DMA_POOL, ptr, size) &&
        !xhgc_mem_is_fixed_dma_target(ptr, size)) {
        printf("[XHGC DCACHE] warning: range is neither DMA_POOL nor fixed DMA target ptr=0x%08lX size=0x%08lX\r\n",
               (unsigned long)(uintptr_t)ptr,
               (unsigned long)size);
    }

    if ((((uintptr_t)ptr | size) & (uintptr_t)(XHGC_DCACHE_LINE_SIZE - 1u)) != 0u) {
        printf("[XHGC DCACHE] warning: range is not 32-byte aligned ptr=0x%08lX size=0x%08lX\r\n",
               (unsigned long)(uintptr_t)ptr,
               (unsigned long)size);
    }
}
#endif

static void xhgc_dcache_apply_range(const void *ptr, size_t size, xhgc_dcache_op_t op)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t aligned_start;
    size_t aligned_size;

    if (ptr == NULL || size == 0u || op == NULL) {
        return;
    }

#if defined(DEBUG)
    xhgc_dcache_debug_check(ptr, size);
#endif

    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0u) {
        return;
    }

    if (!xhgc_range_align_32(ptr, size, &aligned_start, &aligned_size)) {
        return;
    }

    /*
     * Cache maintenance is performed on whole 32-byte lines. The logical DMA
     * range remains [ptr, ptr + size); the aligned cover may include adjacent
     * bytes that share the same cache lines.
     */
    op((uint32_t *)aligned_start, (int32_t)aligned_size);
#else
    (void)ptr;
    (void)size;
    (void)op;
#endif
}

void xhgc_dcache_clean_range(const void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, SCB_CleanDCache_by_Addr);
}

void xhgc_dcache_invalidate_range(void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, (xhgc_dcache_op_t)SCB_InvalidateDCache_by_Addr);
}

void xhgc_dcache_clean_invalidate_range(void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, SCB_CleanInvalidateDCache_by_Addr);
}
