#include "lua.h"
#include "lauxlib.h"
#include "lua_vm.h"
#include "stm32h7xx_hal.h"

static int l_delay(lua_State* L)
{
  lua_Integer ms = luaL_checkinteger(L, 1);
  if (ms < 0) ms = 0;
  if (lua_isyieldable(L)) {
    lua_rt_delay_ms((uint32_t)ms);
    return lua_yield(L, 0);
  }

  HAL_Delay((uint32_t)ms);
  return 0;
}

// 导出全局函数：delay(ms)。在 start/update 协程中调用时会让出 Lua VM。
void lua_register_delay(lua_State* L)
{
  lua_pushcfunction(L, l_delay);
  lua_setglobal(L, "delay");
}
