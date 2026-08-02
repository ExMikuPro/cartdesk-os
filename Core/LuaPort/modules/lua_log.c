#include "lua.h"
#include "lauxlib.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cart_log.h"
#include "lua_foundation.h"
#include "lua_foundation_platform.h"

#define LUA_LOG_MAX_ARGS 16
#define LUA_LOG_MAX_LENGTH 256u

static void append(char* output, size_t output_size, size_t* used,
                   const char* text) {
  if (*used >= output_size - 1u) return;
  int written = snprintf(output + *used, output_size - *used, "%s", text);
  if (written > 0) {
    size_t added = (size_t)written;
    *used += added < output_size - *used ? added : output_size - *used - 1u;
  }
}

static const char* summary(lua_State* L, int index, char* scratch,
                           size_t scratch_size) {
  switch (lua_type(L, index)) {
    case LUA_TSTRING: return lua_tostring(L, index);
    case LUA_TNUMBER:
      if (lua_isinteger(L, index))
        (void)snprintf(scratch, scratch_size, "%lld",
                       (long long)lua_tointeger(L, index));
      else
        (void)snprintf(scratch, scratch_size, "%.14g",
                       (double)lua_tonumber(L, index));
      return scratch;
    case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "true" : "false";
    case LUA_TNIL: return "nil";
    case LUA_TTABLE: return "<table>";
    case LUA_TFUNCTION: return "<function>";
    case LUA_TTHREAD: return "<thread>";
    case LUA_TUSERDATA:
      if (luaL_testudata(L, index, "cartdesk.ui_handle")) return "<ui_handle>";
      if (luaL_testudata(L, index, "cartdesk.asset_handle")) return "<asset_handle>";
      if (luaL_testudata(L, index, "cartdesk.timer_handle")) return "<timer_handle>";
      return "<userdata>";
    default: return "<value>";
  }
}

static int write_log(lua_State* L, cart_log_level_t level) {
  int count = lua_gettop(L);
  if (count > LUA_LOG_MAX_ARGS) {
    lua_pushnil(L);
    lua_pushliteral(L, "log accepts at most 16 arguments");
    return 2;
  }
  lua_foundation_owner_view_t owner;
  if (!lua_foundation_current(L, &owner)) {
    lua_pushnil(L);
    lua_pushliteral(L, "log requires an active application owner");
    return 2;
  }
  uint64_t now = lua_foundation_platform_uptime_ms();
  uint32_t dropped = 0u;
  if (!lua_foundation_log_allow(now, &dropped)) {
    lua_pushboolean(L, 1);
    return 1;
  }
  char message[LUA_LOG_MAX_LENGTH + 1u] = {0};
  size_t used = 0u;
  if (dropped > 0u) {
    char notice[48];
    (void)snprintf(notice, sizeof(notice), "[dropped=%lu] ",
                   (unsigned long)dropped);
    append(message, sizeof(message), &used, notice);
  }
  for (int i = 1; i <= count; ++i) {
    char scratch[48];
    if (i > 1) append(message, sizeof(message), &used, " ");
    append(message, sizeof(message), &used,
           summary(L, i, scratch, sizeof(scratch)));
  }
  if (used == LUA_LOG_MAX_LENGTH) {
    memcpy(message + LUA_LOG_MAX_LENGTH - 3u, "...", 3u);
  }
  CartLog_Write(level, owner.app_id, message);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_debug(lua_State* L) { return write_log(L, CART_LOG_DEBUG); }
static int l_info(lua_State* L) { return write_log(L, CART_LOG_INFO); }
static int l_warn(lua_State* L) { return write_log(L, CART_LOG_WARN); }
static int l_error(lua_State* L) { return write_log(L, CART_LOG_ERROR); }

int luaopen_log(lua_State* L) {
  static const luaL_Reg functions[] = {{"debug", l_debug}, {"info", l_info},
                                        {"warn", l_warn}, {"error", l_error},
                                        {NULL, NULL}};
  luaL_newlib(L, functions);
  return 1;
}
