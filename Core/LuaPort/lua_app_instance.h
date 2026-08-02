#pragma once

#include <stdbool.h>

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建self.state/ui/assets/timers/services五个独立普通table。 */
bool LuaAppInstance_CreateDefaultTables(lua_State* L, int self_index);

#ifdef __cplusplus
}
#endif
