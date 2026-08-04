#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

int luaopen_random(lua_State* L);

#if defined(CARTDESK_HOST_TEST)
typedef bool (*cart_random_u32_provider_t)(uint32_t* value);
void lua_random_set_u32_provider_for_test(cart_random_u32_provider_t provider);
#endif

#ifdef __cplusplus
}
#endif
