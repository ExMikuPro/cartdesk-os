#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>

#include "rng_port.h"

#define LUA_RANDOM_MAX_BYTES 4096u

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L);
  lua_pushstring(L, message);
  return 2;
}

static bool generate_u32(uint32_t* value) {
  return RNG_GetU32(value, NULL) == RNG_OK;
}

static int l_integer(lua_State* L) {
  if (lua_gettop(L) != 2 || !lua_isinteger(L, 1) || !lua_isinteger(L, 2)) {
    return fail(L, "random.integer expects integer min_value and max_value");
  }
  lua_Integer min_value = lua_tointeger(L, 1);
  lua_Integer max_value = lua_tointeger(L, 2);
  if (min_value > max_value) return fail(L, "min_value must be <= max_value");
  uint64_t span = (uint64_t)((int64_t)max_value - (int64_t)min_value) + 1u;
  uint32_t random_value = 0u;
  if (!generate_u32(&random_value)) return fail(L, "random hardware failed");
  uint64_t offset;
  if (span == (UINT64_C(1) << 32)) {
    offset = random_value;
  } else {
    uint32_t range = (uint32_t)span;
    uint32_t threshold = (uint32_t)(0u - range) % range;
    while (random_value < threshold) {
      if (!generate_u32(&random_value)) return fail(L, "random hardware failed");
    }
    offset = random_value % range;
  }
  lua_pushinteger(L, (lua_Integer)((int64_t)min_value + (int64_t)offset));
  return 1;
}

static int l_number(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "random.number expects no arguments");
  uint32_t value = 0u;
  if (!generate_u32(&value)) return fail(L, "random hardware failed");
  lua_pushnumber(L, (lua_Number)value * (1.0 / 4294967296.0));
  return 1;
}

static int l_bytes(lua_State* L) {
  if (lua_gettop(L) != 1 || !lua_isinteger(L, 1)) {
    return fail(L, "random.bytes expects an integer length");
  }
  lua_Integer length = lua_tointeger(L, 1);
  if (length < 0 || length > LUA_RANDOM_MAX_BYTES) {
    return fail(L, "random.bytes length must be 0..4096");
  }
  luaL_Buffer buffer;
  char* bytes = luaL_buffinitsize(L, &buffer, (size_t)length);
  if (RNG_Fill(bytes, (size_t)length, NULL) != RNG_OK) {
    lua_settop(L, 1);
    return fail(L, "random hardware failed");
  }
  luaL_pushresultsize(&buffer, (size_t)length);
  return 1;
}

int luaopen_random(lua_State* L) {
  static const luaL_Reg functions[] = {
      {"integer", l_integer}, {"number", l_number}, {"bytes", l_bytes},
      {NULL, NULL}};
  luaL_newlib(L, functions);
  return 1;
}
