#include "lua.h"
#include "lauxlib.h"
#include "stm32h7xx_hal.h"

static int l_delay(lua_State* L)
{
  lua_Integer ms = luaL_checkinteger(L, 1);
  if (ms < 0) ms = 0;
  HAL_Delay((uint32_t)ms);
  return 0;
}

// 导出全局函数：delay(ms)
void lua_register_delay(lua_State* L)
{
  lua_pushcfunction(L, l_delay);
  lua_setglobal(L, "delay");
}
