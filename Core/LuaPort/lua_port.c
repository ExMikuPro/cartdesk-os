#include "lua_port.h"

#include "lua.h"
#include "lua_ui.h"

int luaopen_gpio(lua_State* L);
int luaopen_pwm(lua_State* L);
int luaopen_tim(lua_State* L);
int luaopen_rng(lua_State* L);
int luaopen_crc(lua_State* L);
void lua_register_delay(lua_State* L);

void lua_port_bind(lua_State* L, const lua_port_config_t* cfg) {
  lua_pushlightuserdata(L, (void*)cfg);
  lua_setfield(L, LUA_REGISTRYINDEX, "port.cfg");

  luaopen_gpio(L);
  lua_setglobal(L, "gpio");
  luaopen_pwm(L);
  lua_setglobal(L, "pwm");
  luaopen_tim(L);
  lua_setglobal(L, "tim");
  luaopen_rng(L);
  lua_setglobal(L, "rng");
  luaopen_crc(L);
  lua_setglobal(L, "crc");

  lua_newtable(L);
  luaopen_ui_label(L);
  lua_setfield(L, -2, "label");
  luaopen_ui_button(L);
  lua_setfield(L, -2, "button");
  luaopen_ui_image(L);
  lua_setfield(L, -2, "image");
  lua_pushcfunction(L, lua_ui_patch);
  lua_setfield(L, -2, "patch");
  lua_setglobal(L, "ui");

  lua_register_delay(L);
}
