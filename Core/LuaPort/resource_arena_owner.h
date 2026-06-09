#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RESOURCE_ARENA_OWNER_NONE = 0,
  RESOURCE_ARENA_OWNER_RESOURCE_MANAGER,
  RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE,
} resource_arena_owner_t;

/**
 * @brief  申请RESOURCE_ARENA运行期所有权
 * @param  owner: 请求成为owner的模块标识
 * @retval true=申请成功, false=owner非法或已有其它owner
 * @note   - 同一时间RESOURCE_ARENA只能有一个owner
 *         - lua_cart_resource_cache默认禁用时不能申请所有权
 */
bool resource_arena_claim(resource_arena_owner_t owner);

/**
 * @brief  释放RESOURCE_ARENA运行期所有权
 * @param  owner: 请求释放的模块标识
 * @retval true=释放成功, false=owner非法或与当前owner不匹配
 * @note   - 只有当前owner可以释放所有权
 */
bool resource_arena_release(resource_arena_owner_t owner);

/**
 * @brief  获取当前RESOURCE_ARENA owner
 * @retval RESOURCE_ARENA_OWNER_NONE=当前无owner, 其它值=当前owner
 */
resource_arena_owner_t resource_arena_get_owner(void);

/**
 * @brief  获取RESOURCE_ARENA owner的日志名称
 * @param  owner: owner枚举值
 * @return 非NULL=owner名称字符串
 * @note   - 未识别的owner返回"UNKNOWN"
 */
const char *resource_arena_owner_name(resource_arena_owner_t owner);

#if XHGC_MEMINFO_SELFTEST_ENABLE

/**
 * @brief  执行RESOURCE_ARENA owner状态机自检
 * @retval true=自检通过, false=自检失败
 * @note   - 本函数会申请和释放resource_manager owner
 *         - 自检结束时预期owner恢复为RESOURCE_ARENA_OWNER_NONE
 */
bool resource_arena_owner_selftest(void);
#endif

#ifdef __cplusplus
}
#endif
