#include "resource_arena_owner.h"

#include <stdio.h>

#ifndef XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE
#define XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE 0
#endif

static resource_arena_owner_t s_resource_arena_owner = RESOURCE_ARENA_OWNER_NONE;

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

resource_arena_owner_t resource_arena_get_owner(void)
{
  return s_resource_arena_owner;
}

#if XHGC_MEMINFO_SELFTEST_ENABLE
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
