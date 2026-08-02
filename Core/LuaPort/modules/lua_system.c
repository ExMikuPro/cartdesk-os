#include "lua.h"
#include "lauxlib.h"

#include "lua_foundation_platform.h"
#include "lua_foundation.h"

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L); lua_pushstring(L, message); return 2;
}

static int l_screen_size(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.screen_size expects no arguments");
  uint32_t width, height;
  if (!lua_foundation_platform_screen_size(&width, &height))
    return fail(L, "no active display");
  lua_pushinteger(L, (lua_Integer)width);
  lua_pushinteger(L, (lua_Integer)height);
  return 2;
}

static int l_firmware_version(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.firmware_version expects no arguments");
  lua_pushstring(L, lua_foundation_platform_firmware_version());
  return 1;
}

static int l_uptime_ms(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.uptime_ms expects no arguments");
  uint64_t uptime = lua_foundation_platform_uptime_ms();
  if (uptime <= (uint64_t)LUA_MAXINTEGER) lua_pushinteger(L, (lua_Integer)uptime);
  else lua_pushnumber(L, (lua_Number)uptime);
  return 1;
}

static int l_memory_info(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.memory_info expects no arguments");
  lua_foundation_memory_info_t info;
  if (!lua_foundation_platform_memory_info(&info)) return fail(L, "memory snapshot unavailable");
  lua_createtable(L, 0, 6);
#define SET_INT(name) do { lua_pushinteger(L, (lua_Integer)info.name); lua_setfield(L, -2, #name); } while (0)
  SET_INT(lua_used); SET_INT(lua_free); SET_INT(resource_used);
  SET_INT(resource_free); SET_INT(freertos_heap_free); SET_INT(freertos_heap_min);
#undef SET_INT
  return 1;
}

static int l_sd_status(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.sd_status expects no arguments");
  bool available, mounted;
  lua_foundation_platform_sd_status(&available, &mounted);
  lua_createtable(L, 0, 2);
  lua_pushboolean(L, available); lua_setfield(L, -2, "available");
  lua_pushboolean(L, mounted); lua_setfield(L, -2, "mounted");
  return 1;
}

static int l_usb_status(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.usb_status expects no arguments");
  bool initialized, configured, connected;
  lua_foundation_platform_usb_status(&initialized, &configured, &connected);
  lua_createtable(L, 0, 4);
  lua_pushboolean(L, initialized); lua_setfield(L, -2, "initialized");
  lua_pushboolean(L, configured); lua_setfield(L, -2, "configured");
  lua_pushboolean(L, connected); lua_setfield(L, -2, "connected");
  lua_pushliteral(L, "cdc"); lua_setfield(L, -2, "class");
  return 1;
}

static int l_exit(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.exit expects no arguments");
  if (!lua_foundation_current(L, NULL))
    return fail(L, "system.exit requires an active application owner");
  if (!lua_foundation_platform_request_exit()) return fail(L, "exit request rejected");
  lua_pushboolean(L, 1); return 1;
}

static int l_restart(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "system.restart_app expects no arguments");
  if (!lua_foundation_current(L, NULL))
    return fail(L, "system.restart_app requires an active application owner");
  if (!lua_foundation_platform_request_restart()) return fail(L, "restart request rejected");
  lua_pushboolean(L, 1); return 1;
}

int luaopen_system(lua_State* L) {
  static const luaL_Reg functions[] = {
      {"screen_size", l_screen_size}, {"firmware_version", l_firmware_version},
      {"uptime_ms", l_uptime_ms}, {"memory_info", l_memory_info},
      {"sd_status", l_sd_status}, {"usb_status", l_usb_status},
      {"exit", l_exit}, {"restart_app", l_restart}, {NULL, NULL}};
  luaL_newlib(L, functions); return 1;
}
