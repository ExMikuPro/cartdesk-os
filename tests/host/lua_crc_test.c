#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

int luaopen_crc(lua_State* L);

uint32_t CRC32_IEEE_Calculate(const void* data, uint32_t size) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8u; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return crc ^ 0xFFFFFFFFu;
}

static void check(lua_State* L, const void* data, size_t size, uint32_t expected) {
  lua_settop(L, 0);
  lua_pushlstring(L, (const char*)data, size);
  assert(luaopen_crc(L) == 1);
  lua_getfield(L, -1, "crc32");
  lua_pushvalue(L, 1);
  assert(lua_pcall(L, 1, 1, 0) == LUA_OK);
  assert((uint32_t)lua_tonumber(L, -1) == expected);
}

int main(void) {
  lua_State* L = luaL_newstate();
  assert(L);
  check(L, "", 0, 0x00000000u);
  check(L, "hello", 5, 0x3610A686u);
  check(L, "123456789", 9, 0xCBF43926u);
  const uint8_t binary[] = {0, 1, 2, 3};
  check(L, binary, sizeof(binary), 0x8BB98613u);
  check(L, "a", 1, 0xE8B7BE43u);
  check(L, "abc", 3, 0x352441C2u);
  check(L, "abcd", 4, 0xED82CD11u);
  check(L, "abcde", 5, 0x8587D865u);

  lua_settop(L, 0);
  assert(luaopen_crc(L) == 1);
  lua_getfield(L, -1, "verify32");
  lua_pushliteral(L, "hello");
  lua_pushnumber(L, 0x3610A686u);
  assert(lua_pcall(L, 2, 1, 0) == LUA_OK && lua_toboolean(L, -1));
  lua_settop(L, 1);
  lua_getfield(L, 1, "verify32");
  lua_pushliteral(L, "hello");
  lua_pushinteger(L, -1);
  assert(lua_pcall(L, 2, 2, 0) == LUA_OK);
  assert(lua_isnil(L, -2) && lua_isstring(L, -1));
  lua_settop(L, 1);
  lua_getfield(L, 1, "crc32");
  lua_newtable(L);
  assert(lua_pcall(L, 1, 2, 0) == LUA_OK);
  assert(lua_isnil(L, -2) && lua_isstring(L, -1));
  lua_close(L);
  return 0;
}
