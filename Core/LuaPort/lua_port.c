#include "lua_port.h"

#include "lua.h"
#include "lauxlib.h"

// modules 里提供的导出函数（确保和你的 .c 文件里一致）
int  luaopen_gpio(lua_State* L);
int  luaopen_tim(lua_State* L);
int  luaopen_sd(lua_State* L);
void lua_register_delay(lua_State* L);

void lua_port_bind(lua_State* L, const lua_port_config_t* cfg)
{
  // cfg 放 registry，给 gpio 模块取 LED 引脚用
  lua_pushlightuserdata(L, (void*)cfg);
  lua_setfield(L, LUA_REGISTRYINDEX, "port.cfg");

  // gpio.xxx
  luaopen_gpio(L);
  lua_setglobal(L, "gpio");

  // tim.xxx
  luaopen_tim(L);
  lua_setglobal(L, "tim");

  // sd.xxx
  luaopen_sd(L);
  lua_setglobal(L, "sd");


  // delay(ms) -> HAL_Delay(ms)
  lua_register_delay(L);
}
