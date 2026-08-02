#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "crc.h"

#define LUA_CRC_MAX_INPUT (1024u * 1024u)

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L);
  lua_pushstring(L, message);
  return 2;
}

static bool read_data(lua_State* L, int index, const char** data, size_t* size) {
  if (lua_type(L, index) != LUA_TSTRING) return false;
  *data = lua_tolstring(L, index, size);
  return *size <= LUA_CRC_MAX_INPUT;
}

static int l_crc32(lua_State* L) {
  const char* data = NULL;
  size_t size = 0u;
  if (lua_gettop(L) != 1 || !read_data(L, 1, &data, &size)) {
    return fail(L, "crc.crc32 expects a binary string up to 1 MiB");
  }
  uint32_t value = CRC32_IEEE_Calculate(data, (uint32_t)size);
  if (value <= (uint32_t)LUA_MAXINTEGER) lua_pushinteger(L, (lua_Integer)value);
  else lua_pushnumber(L, (lua_Number)value);
  return 1;
}

static int l_verify32(lua_State* L) {
  const char* data = NULL;
  size_t size = 0u;
  if (lua_gettop(L) != 2 || !read_data(L, 1, &data, &size)) {
    return fail(L, "crc.verify32 expects data and expected_crc");
  }
  if (!lua_isnumber(L, 2)) {
    return fail(L, "expected_crc must be an integer in 0..0xFFFFFFFF");
  }
  lua_Number expected_number = lua_tonumber(L, 2);
  if (!isfinite(expected_number) || expected_number < 0.0 ||
      expected_number > 4294967295.0 || floor(expected_number) != expected_number)
    return fail(L, "expected_crc must be an integer in 0..0xFFFFFFFF");
  uint32_t expected = (uint32_t)expected_number;
  lua_pushboolean(L, CRC32_IEEE_Calculate(data, (uint32_t)size) ==
                         expected);
  return 1;
}

int luaopen_crc(lua_State* L) {
  static const luaL_Reg functions[] = {
      {"crc32", l_crc32}, {"verify32", l_verify32}, {NULL, NULL}};
  luaL_newlib(L, functions);
  return 1;
}
