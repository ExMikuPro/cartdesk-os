#include "lua_port.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lua_ui.h"

// modules 里提供的导出函数（确保和你的 .c 文件里一致）
int  luaopen_gpio(lua_State* L);
int  luaopen_pwm(lua_State* L);
int  luaopen_tim(lua_State* L);
int  luaopen_rng(lua_State* L);
int  luaopen_crc(lua_State* L);
void lua_register_delay(lua_State* L);

static bool lua_ui_is_drawable(lua_State* L, int idx)
{
  return lua_ui_button_is(L, idx) || lua_ui_slider_is(L, idx) || lua_ui_image_is(L, idx);
}

static const char* lua_ui_drawable_id(lua_State* L, int idx)
{
  const char* id = lua_ui_button_id(L, idx);
  if (id) return id;
  id = lua_ui_slider_id(L, idx);
  if (id) return id;
  return lua_ui_image_id(L, idx);
}

static bool lua_ui_push_find_in_value(lua_State* L, int idx, const char* id)
{
  idx = lua_absindex(L, idx);

  if (lua_ui_is_drawable(L, idx)) {
    const char* drawable_id = lua_ui_drawable_id(L, idx);
    if (drawable_id && drawable_id[0] != '\0' && strcmp(drawable_id, id) == 0) {
      lua_pushvalue(L, idx);
      return true;
    }
    return false;
  }

  if (!lua_istable(L, idx)) return false;

  lua_getfield(L, idx, "children");
  if (!lua_isnil(L, -1) && lua_ui_push_find_in_value(L, -1, id)) {
    lua_remove(L, -2);
    return true;
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    if (lua_ui_push_find_in_value(L, -1, id)) {
      lua_insert(L, -3);
      lua_pop(L, 2);
      return true;
    }
    lua_pop(L, 1);
  }

  return false;
}

static int l_ui_find(lua_State* L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  const char* id = luaL_checkstring(L, 2);

  lua_getfield(L, 1, "children");
  if (!lua_isnil(L, -1) && lua_ui_push_find_in_value(L, -1, id)) {
    lua_remove(L, -2);
    return 1;
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  return 1;
}

static int l_ui_patch(lua_State* L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  const char* id = luaL_checkstring(L, 2);
  luaL_checktype(L, 3, LUA_TTABLE);

  lua_getfield(L, 1, "children");
  if (lua_isnil(L, -1) || !lua_ui_push_find_in_value(L, -1, id)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_pushliteral(L, "ui id not found");
    return 2;
  }
  lua_remove(L, -2);

  if (lua_ui_button_is(L, -1)) {
    (void)lua_ui_button_patch(L, -1, 3);
  } else if (lua_ui_slider_is(L, -1)) {
    (void)lua_ui_slider_patch(L, -1, 3);
  } else if (lua_ui_image_is(L, -1)) {
    if (lua_ui_image_patch(L, -1, 3) != 0) {
      lua_pop(L, 1);
      lua_pushnil(L);
      lua_pushliteral(L, "patching image src is not supported");
      return 2;
    }
  } else {
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_pushliteral(L, "ui id not found");
    return 2;
  }

  lua_pop(L, 1);
  lua_pushboolean(L, 1);
  return 1;
}

void lua_ui_delete_children(lua_State* L, int idx)
{
  if (!L) return;
  idx = lua_absindex(L, idx);

  if (lua_ui_button_is(L, idx)) {
    lua_ui_button_delete(L, idx);
    return;
  }
  if (lua_ui_slider_is(L, idx)) {
    lua_ui_slider_delete(L, idx);
    return;
  }
  if (lua_ui_image_is(L, idx)) {
    lua_ui_image_delete(L, idx);
    return;
  }
  if (!lua_istable(L, idx)) return;

  lua_getfield(L, idx, "children");
  if (!lua_isnil(L, -1)) {
    lua_ui_delete_children(L, -1);
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    lua_ui_delete_children(L, -1);
    lua_pop(L, 1);
  }
}

void lua_port_bind(lua_State* L, const lua_port_config_t* cfg)
{
  // cfg 放 registry，给 gpio 模块取 LED 引脚用
  lua_pushlightuserdata(L, (void*)cfg);
  lua_setfield(L, LUA_REGISTRYINDEX, "port.cfg");

  // gpio.xxx
  luaopen_gpio(L);
  lua_setglobal(L, "gpio");

  luaopen_pwm(L);
  lua_setglobal(L, "pwm");

  // tim.xxx
  luaopen_tim(L);
  lua_setglobal(L, "tim");

  // rng.xxx
  luaopen_rng(L);
  lua_setglobal(L, "rng");

  // crc.xxx
  luaopen_crc(L);
  lua_setglobal(L, "crc");

  // ui 命名空间
  lua_newtable(L);
  
  // ui.button 模块
  luaopen_ui_button(L);
  lua_setfield(L, -2, "button");

  // ui.slider 模块  <-- 添加这部分
  luaopen_ui_slider(L);
  lua_setfield(L, -2, "slider");

  luaopen_ui_image(L);
  lua_setfield(L, -2, "image");

  lua_pushcfunction(L, l_ui_find);
  lua_setfield(L, -2, "find");

  lua_pushcfunction(L, l_ui_patch);
  lua_setfield(L, -2, "patch");
  
  // 设置 ui 为全局变量
  lua_setglobal(L, "ui");

  // delay(ms) -> HAL_Delay(ms)
  lua_register_delay(L);
}
