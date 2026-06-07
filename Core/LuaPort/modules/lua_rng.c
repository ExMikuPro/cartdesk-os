#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>

#include "rng.h"

#define LUA_RNG_MAX_BYTES 4096

static int lua_rng_fail(lua_State *L)
{
  lua_pushnil(L);
  lua_pushstring(L, "rng hardware failed");
  return 2;
}

static int lua_rng_generate_u32(uint32_t *out)
{
  if (out == NULL || hrng.Instance != RNG) {
    return -1;
  }

  return (HAL_RNG_GenerateRandomNumber(&hrng, out) == HAL_OK) ? 0 : -1;
}

static int l_rng_u32(lua_State *L)
{
  uint32_t value = 0u;
  if (lua_rng_generate_u32(&value) != 0) {
    return lua_rng_fail(L);
  }

  lua_pushinteger(L, (lua_Integer)value);
  return 1;
}

static int l_rng_bytes(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  luaL_argcheck(L, n >= 0, 1, "byte count must be non-negative");
  luaL_argcheck(L, n <= LUA_RNG_MAX_BYTES, 1, "byte count too large");

  if (n == 0) {
    lua_pushliteral(L, "");
    return 1;
  }

  luaL_Buffer b;
  char *buf = luaL_buffinitsize(L, &b, (size_t)n);
  size_t written = 0u;

  while (written < (size_t)n) {
    uint32_t value = 0u;
    if (lua_rng_generate_u32(&value) != 0) {
      return lua_rng_fail(L);
    }

    for (size_t i = 0u; i < sizeof(value) && written < (size_t)n; ++i) {
      buf[written++] = (char)((value >> (i * 8u)) & 0xFFu);
    }
  }

  luaL_pushresultsize(&b, (size_t)n);
  return 1;
}

static const luaL_Reg rng_funcs[] = {
  {"u32", l_rng_u32},
  {"bytes", l_rng_bytes},
  {NULL, NULL}
};

int luaopen_rng(lua_State *L)
{
  luaL_newlib(L, rng_funcs);
  return 1;
}
