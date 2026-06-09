#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *base;
  uint8_t *ptr;
  uint8_t *end;
} app_arena_t;

typedef uint8_t *app_arena_mark_t;

/**
 * @brief  初始化线性arena的地址范围
 * @param  a: arena实例指针
 * @param  base: arena起始地址
 * @param  size: arena可用字节数
 * @retval None
 * @note   - 本函数只设置base/ptr/end，不清零底层内存
 *         - 若base+size发生回绕，arena会被置为空
 */
void app_arena_init(app_arena_t *a, void *base, size_t size);

/**
 * @brief  从线性arena分配一块对齐内存
 * @param  a: arena实例指针
 * @param  size: 申请字节数
 * @param  align: 对齐字节数，必须为2的幂
 * @return 非NULL=分配成功返回对齐后的指针, NULL=参数非法或空间不足
 * @note   - 本函数只推进arena指针，不支持单块释放
 *         - 若arena位于APP_ARENA_REST，会按实际消耗字节数记录meminfo统计
 */
void *app_arena_alloc(app_arena_t *a, size_t size, size_t align);

/**
 * @brief  获取当前arena回滚标记
 * @param  a: arena实例指针
 * @return 非NULL=当前写入位置, NULL=参数非法
 */
app_arena_mark_t app_arena_mark(app_arena_t *a);

/**
 * @brief  将arena回滚到指定标记位置
 * @param  a: arena实例指针
 * @param  mark: 由app_arena_mark返回的标记位置
 * @retval None
 * @note   - mark必须位于当前arena的base到end范围内
 *         - 若arena位于APP_ARENA_REST，会释放回滚区间对应的meminfo统计
 */
void app_arena_reset_to(app_arena_t *a, app_arena_mark_t mark);

/**
 * @brief  将arena重置到起始位置
 * @param  a: arena实例指针
 * @retval None
 * @note   - 本函数不清零底层内存
 *         - 若arena位于APP_ARENA_REST，会重置RESOURCE标签的meminfo used统计
 */
void app_arena_reset(app_arena_t *a);

/**
 * @brief  查询arena已使用字节数
 * @param  a: arena实例指针
 * @return 当前ptr相对base的偏移字节数，参数非法时返回0
 */
size_t app_arena_used(const app_arena_t *a);

/**
 * @brief  查询arena剩余字节数
 * @param  a: arena实例指针
 * @return 当前ptr到end之间的剩余字节数，参数非法时返回0
 */
size_t app_arena_remaining(const app_arena_t *a);
#if XHGC_MEMINFO_SELFTEST_ENABLE

/**
 * @brief  执行app_arena与meminfo联动自检
 * @retval true=自检通过, false=自检失败
 * @note   - 本函数会初始化RESOURCE_ARENA上的临时arena并执行一次分配/重置
 *         - 本函数会打印meminfo快照和RESOURCE_ARENA owner自检日志
 */
bool app_arena_meminfo_selftest(void);
#endif

#ifdef __cplusplus
}
#endif
