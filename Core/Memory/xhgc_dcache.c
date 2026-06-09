#include "xhgc_dcache.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "xhgc_memory_layout.h"

#define XHGC_DCACHE_LINE_SIZE 32u

typedef void (*xhgc_dcache_op_t)(uint32_t *addr, int32_t size);

/**
 * @brief  将逻辑DCache维护范围扩展到32字节cache line边界
 * @param  ptr: 逻辑起始地址
 * @param  size: 逻辑范围字节数
 * @param  aligned_start: 输出对齐后的起始地址
 * @param  aligned_size: 输出对齐后的维护字节数
 * @retval true=范围可维护, false=参数非法、地址溢出或长度超过CMSIS接口限制
 * @note   - 本函数只计算地址覆盖范围，不执行cache维护
 *         - 输出范围可能覆盖逻辑范围前后的同一cache line相邻字节
 */
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
/**
 * @brief  调试模式下判断范围是否完全落入指定内存zone
 * @param  zone_id: 目标内存zone
 * @param  ptr: 待检查起始地址
 * @param  size: 待检查字节数
 * @retval true=范围完整位于zone内, false=参数非法、长度超出32位或范围越界
 * @note   - 本函数只用于DCache维护前的调试检查
 *         - size超过xhgc_mem_addr_in_zone的32位入参能力时直接拒绝
 */
static bool xhgc_range_in_zone(XHGC_MemZoneId zone_id, const void *ptr, size_t size)
{
    if (size > UINT32_MAX) {
        return false;
    }

    return xhgc_mem_addr_in_zone(zone_id, (uintptr_t)ptr, (uint32_t)size);
}

/**
 * @brief  调试模式下判断范围是否位于整体SDRAM地址窗口内
 * @param  ptr: 待检查起始地址
 * @param  size: 待检查字节数
 * @retval true=范围位于SDRAM窗口内, false=参数非法或地址溢出/越界
 * @note   - 本函数不检查具体zone归属，只做SDRAM总范围检查
 */
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

/**
 * @brief  调试模式下检查DCache维护范围的DMA安全性
 * @param  ptr: 逻辑起始地址
 * @param  size: 逻辑范围字节数
 * @retval None
 * @note   - 本函数仅打印告警，不阻止后续cache维护
 *         - 会检查范围是否在SDRAM、DMA_POOL或固定DMA目标区内
 *         - 未按32字节对齐的调用会打印告警，但实际维护仍由调用方扩展覆盖
 */
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

/**
 * @brief  对逻辑地址范围执行一次DCache维护操作
 * @param  ptr: 逻辑起始地址
 * @param  size: 逻辑范围字节数
 * @param  op: CMSIS DCache维护函数指针
 * @retval None
 * @note   - 参数非法、DCache未使能或平台未声明DCache时直接返回
 *         - 实际维护范围会向外扩展到完整32字节cache line
 *         - 调用方需保证该范围适合执行clean/invalidate对应的DMA同步语义
 */
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

/**
 * @brief  清理指定地址范围对应的DCache
 * @param  ptr: 需要清理的起始地址
 * @param  size: 需要清理的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 用于CPU写入、DMA读取前的cache一致性维护
 */
void xhgc_dcache_clean_range(const void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, SCB_CleanDCache_by_Addr);
}

/**
 * @brief  失效指定地址范围对应的DCache
 * @param  ptr: 需要失效的起始地址
 * @param  size: 需要失效的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 用于DMA写入、CPU读取前的cache一致性维护
 */
void xhgc_dcache_invalidate_range(void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, (xhgc_dcache_op_t)SCB_InvalidateDCache_by_Addr);
}

/**
 * @brief  清理并失效指定地址范围对应的DCache
 * @param  ptr: 需要清理并失效的起始地址
 * @param  size: 需要清理并失效的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 仅在DCache存在且已使能时执行硬件cache维护
 */
void xhgc_dcache_clean_invalidate_range(void *ptr, size_t size)
{
    xhgc_dcache_apply_range(ptr, size, SCB_CleanInvalidateDCache_by_Addr);
}
