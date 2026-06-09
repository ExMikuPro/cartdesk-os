#ifndef XHGC_DCACHE_H
#define XHGC_DCACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  清理指定地址范围对应的DCache
 * @param  ptr: 需要清理的起始地址
 * @param  size: 需要清理的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 用于CPU写入、DMA读取前的cache一致性维护
 */
void xhgc_dcache_clean_range(const void *ptr, size_t size);

/**
 * @brief  失效指定地址范围对应的DCache
 * @param  ptr: 需要失效的起始地址
 * @param  size: 需要失效的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 用于DMA写入、CPU读取前的cache一致性维护
 */
void xhgc_dcache_invalidate_range(void *ptr, size_t size);

/**
 * @brief  清理并失效指定地址范围对应的DCache
 * @param  ptr: 需要清理并失效的起始地址
 * @param  size: 需要清理并失效的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到完整32字节cache line
 *         - 仅在DCache存在且已使能时执行硬件cache维护
 */
void xhgc_dcache_clean_invalidate_range(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_DCACHE_H */
