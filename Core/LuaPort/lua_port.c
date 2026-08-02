#include "lua_port.h"

#include "lauxlib.h"
#include "lua.h"
#include "lua_ui.h"

int luaopen_assets(lua_State* L);
int luaopen_crc(lua_State* L);
int luaopen_log(lua_State* L);
int luaopen_random(lua_State* L);
int luaopen_storage(lua_State* L);
int luaopen_system(lua_State* L);
int luaopen_timer(lua_State* L);

static int readonly_newindex(lua_State* L) {
  return luaL_error(L, "CartDesk core modules are read-only");
}

static void publish_readonly(lua_State* L, const char* name) {
  /* Stack: mutable implementation table. Publish a proxy so applications
   * cannot replace core API functions, while C keeps the implementation
   * table private in the proxy metatable. */
  lua_newtable(L);
  lua_newtable(L);
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, readonly_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);
  lua_remove(L, -2);
  lua_setglobal(L, name);
}

static void open_ui(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, lua_ui_root);
  lua_setfield(L, -2, "root");
  luaopen_ui_container(L);
  lua_setfield(L, -2, "container");
  luaopen_ui_label(L);
  lua_setfield(L, -2, "label");
  luaopen_ui_button(L);
  lua_setfield(L, -2, "button");
  luaopen_ui_image(L);
  lua_setfield(L, -2, "image");
  lua_pushcfunction(L, lua_ui_patch);
  lua_setfield(L, -2, "patch");
  lua_pushcfunction(L, lua_ui_delete);
  lua_setfield(L, -2, "delete");
}

void lua_port_bind(lua_State* L, const lua_port_config_t* cfg) {
  lua_pushlightuserdata(L, (void*)cfg);
  lua_setfield(L, LUA_REGISTRYINDEX, "port.cfg");

  open_ui(L);          publish_readonly(L, "ui");
  luaopen_assets(L);   publish_readonly(L, "assets");
  luaopen_storage(L);  publish_readonly(L, "storage");
  luaopen_timer(L);    publish_readonly(L, "timer");
  luaopen_system(L);   publish_readonly(L, "system");
  luaopen_random(L);   publish_readonly(L, "random");
  luaopen_log(L);      publish_readonly(L, "log");
  luaopen_crc(L);      publish_readonly(L, "crc");
}
