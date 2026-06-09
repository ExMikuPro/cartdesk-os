#include "resource_arena_owner.h"

#include <stdio.h>

#ifndef XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE
#define XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE 0
#endif

static resource_arena_owner_t s_resource_arena_owner = RESOURCE_ARENA_OWNER_NONE;

/**
 * @brief  获取RESOURCE_ARENA owner的日志名称
 * @param  owner: owner枚举值
 * @return 非NULL=owner名称字符串
 * @note   - 未识别的owner返回"UNKNOWN"
 */
const char *resource_arena_owner_name(resource_arena_owner_t owner)
{
  switch (owner) {
    case RESOURCE_ARENA_OWNER_NONE:
      return "NONE";
    case RESOURCE_ARENA_OWNER_RESOURCE_MANAGER:
      return "resource_manager";
    case RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE:
      return "lua_cart_resource_cache";
    default:
      return "UNKNOWN";
  }
}

static void resource_arena_owner_log(const char *action,
                                     resource_arena_owner_t owner,
                                     bool ok,
                                     const char *reason)
{
#if XHGC_RESOURCE_ARENA_OWNER_DEBUG_LOG
  printf("[RESOURCE_ARENA owner] %s owner=%s current=%s %s%s%s\r\n",
         action,
         resource_arena_owner_name(owner),
         resource_arena_owner_name(s_resource_arena_owner),
         ok ? "OK" : "FAIL",
         reason ? " reason=" : "",
         reason ? reason : "");
#else
  (void)action;
  (void)owner;
  (void)ok;
  (void)reason;
#endif
}

/**
 * @brief  申请RESOURCE_ARENA运行期所有权
 * @param  owner: 请求成为owner的模块标识
 * @retval true=申请成功, false=owner非法或已有其它owner
 * @note   - 同一时间RESOURCE_ARENA只能有一个owner
 *         - lua_cart_resource_cache默认禁用时不能申请所有权
 */
bool resource_arena_claim(resource_arena_owner_t owner)
{
  if (owner == RESOURCE_ARENA_OWNER_NONE) {
    resource_arena_owner_log("claim", owner, false, "invalid owner");
    return false;
  }

  if (owner == RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE &&
      !XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE) {
    resource_arena_owner_log("claim",
                             owner,
                             false,
                             "lua_cart_resource_cache disabled: resource_manager owns RESOURCE_ARENA");
    return false;
  }

  if (s_resource_arena_owner == owner) {
    resource_arena_owner_log("claim", owner, true, "already owner");
    return true;
  }

  if (s_resource_arena_owner != RESOURCE_ARENA_OWNER_NONE) {
    resource_arena_owner_log("claim", owner, false, "illegal double owner attempt");
    return false;
  }

  s_resource_arena_owner = owner;
  resource_arena_owner_log("claim", owner, true, NULL);
  return true;
}

/**
 * @brief  释放RESOURCE_ARENA运行期所有权
 * @param  owner: 请求释放的模块标识
 * @retval true=释放成功, false=owner非法或与当前owner不匹配
 * @note   - 只有当前owner可以释放所有权
 */
bool resource_arena_release(resource_arena_owner_t owner)
{
  if (owner == RESOURCE_ARENA_OWNER_NONE) {
    resource_arena_owner_log("release", owner, false, "invalid owner");
    return false;
  }

  if (s_resource_arena_owner != owner) {
    resource_arena_owner_log("release", owner, false, "owner mismatch");
    return false;
  }

  s_resource_arena_owner = RESOURCE_ARENA_OWNER_NONE;
  resource_arena_owner_log("release", owner, true, NULL);
  return true;
}

/**
 * @brief  获取当前RESOURCE_ARENA owner
 * @retval RESOURCE_ARENA_OWNER_NONE=当前无owner, 其它值=当前owner
 */
resource_arena_owner_t resource_arena_get_owner(void)
{
  return s_resource_arena_owner;
}

#if XHGC_MEMINFO_SELFTEST_ENABLE
/**
 * @brief  执行RESOURCE_ARENA owner状态机自检
 * @retval true=自检通过, false=自检失败
 * @note   - 本函数会申请和释放resource_manager owner
 *         - 自检结束时预期owner恢复为RESOURCE_ARENA_OWNER_NONE
 */
bool resource_arena_owner_selftest(void)
{
  bool pass = true;

  printf("[RESOURCE_ARENA owner selftest] baseline owner=%s\r\n",
         resource_arena_owner_name(resource_arena_get_owner()));

  pass = pass && resource_arena_get_owner() == RESOURCE_ARENA_OWNER_NONE;
  pass = pass && resource_arena_claim(RESOURCE_ARENA_OWNER_RESOURCE_MANAGER);
  pass = pass && resource_arena_get_owner() == RESOURCE_ARENA_OWNER_RESOURCE_MANAGER;
  pass = pass && !resource_arena_claim(RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE);
  pass = pass && resource_arena_get_owner() == RESOURCE_ARENA_OWNER_RESOURCE_MANAGER;
  pass = pass && resource_arena_release(RESOURCE_ARENA_OWNER_RESOURCE_MANAGER);
  pass = pass && resource_arena_get_owner() == RESOURCE_ARENA_OWNER_NONE;

#if !XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE
  pass = pass && !resource_arena_claim(RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE);
  pass = pass && resource_arena_get_owner() == RESOURCE_ARENA_OWNER_NONE;
#endif

  printf("[RESOURCE_ARENA owner selftest] %s owner=%s\r\n",
         pass ? "PASS" : "FAIL",
         resource_arena_owner_name(resource_arena_get_owner()));
  return pass;
}
#endif
