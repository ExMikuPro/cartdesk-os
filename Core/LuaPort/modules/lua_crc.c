#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <stdio.h>

#include "crc.h"

static int lua_crc_fail(lua_State *L)
{
  lua_pushnil(L);
  lua_pushstring(L, "crc hardware failed");
  return 2;
}

static int lua_crc32_ieee(lua_State *L, const void *data, size_t len, uint32_t *out)
{
  if ((data == NULL && len != 0u) || out == NULL || hcrc.Instance != CRC) {
    return -1;
  }
  if (len > UINT32_MAX) {
    return luaL_error(L, "crc input too large");
  }

  /*
   * MX_CRC_Init configures the STM32 CRC peripheral for IEEE CRC32:
   * poly 0x04C11DB7, init 0xFFFFFFFF, byte input inversion, output inversion.
   * CRC32_IEEE_Calculate applies the final xor 0xFFFFFFFF.
   */
  *out = CRC32_IEEE_Calculate(data, (uint32_t)len);
  return 0;
}

static int l_crc_crc32(lua_State *L)
{
  size_t len = 0u;
  const char *data = luaL_checklstring(L, 1, &len);

  uint32_t value = 0u;
  if (lua_crc32_ieee(L, data, len, &value) != 0) {
    return lua_crc_fail(L);
  }

  lua_pushinteger(L, (lua_Integer)value);
  return 1;
}

static int l_crc_crc32_hex(lua_State *L)
{
  size_t len = 0u;
  const char *data = luaL_checklstring(L, 1, &len);

  uint32_t value = 0u;
  if (lua_crc32_ieee(L, data, len, &value) != 0) {
    return lua_crc_fail(L);
  }

  char hex[9];
  snprintf(hex, sizeof(hex), "%08lX", (unsigned long)value);
  lua_pushlstring(L, hex, 8u);
  return 1;
}

static const luaL_Reg crc_funcs[] = {
  {"crc32", l_crc_crc32},
  {"crc32_hex", l_crc_crc32_hex},
  {NULL, NULL}
};

int luaopen_crc(lua_State *L)
{
  luaL_newlib(L, crc_funcs);
  return 1;
}
