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

bool resource_arena_claim(resource_arena_owner_t owner);
bool resource_arena_release(resource_arena_owner_t owner);
resource_arena_owner_t resource_arena_get_owner(void);
const char *resource_arena_owner_name(resource_arena_owner_t owner);

#if XHGC_MEMINFO_SELFTEST_ENABLE
bool resource_arena_owner_selftest(void);
#endif

#ifdef __cplusplus
}
#endif
