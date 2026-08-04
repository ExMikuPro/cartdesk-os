#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "lua_random.h"
#include "rng_port.h"

static uint32_t g_values[8];
static size_t g_value_count;
static size_t g_value_index;
static bool g_provider_fails;

static bool fixed_provider(uint32_t* value) {
  if (g_provider_fails || value == NULL || g_value_index >= g_value_count) {
    return false;
  }
  *value = g_values[g_value_index++];
  return true;
}

RNG_Status RNG_GetU32(uint32_t* out, void* error_info) {
  (void)error_info;
  return fixed_provider(out) ? RNG_OK : RNG_E_TEST_FAILURE;
}

RNG_Status RNG_Fill(void* buffer, size_t length, void* error_info) {
  (void)error_info;
  if (buffer != NULL) memset(buffer, 0, length);
  return RNG_OK;
}

static void set_values(const uint32_t* values, size_t count) {
  assert(count <= sizeof(g_values) / sizeof(g_values[0]));
  if (count > 0u) memcpy(g_values, values, count * sizeof(values[0]));
  g_value_count = count;
  g_value_index = 0u;
  g_provider_fails = false;
}

static int push_integer_function(lua_State* L) {
  lua_getglobal(L, "random");
  lua_getfield(L, -1, "integer");
  lua_remove(L, -2);
  return lua_gettop(L);
}

static lua_Integer call_success(lua_State* L, lua_Integer minimum,
                                lua_Integer maximum) {
  int base = lua_gettop(L);
  (void)push_integer_function(L);
  lua_pushinteger(L, minimum);
  lua_pushinteger(L, maximum);
  assert(lua_pcall(L, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_gettop(L) == base + 1);
  assert(lua_isinteger(L, -1));
  lua_Integer result = lua_tointeger(L, -1);
  assert(result >= minimum && result <= maximum);
  lua_settop(L, base);
  return result;
}

static void call_failure(lua_State* L, lua_Integer minimum,
                         lua_Integer maximum, const char* expected) {
  int base = lua_gettop(L);
  (void)push_integer_function(L);
  lua_pushinteger(L, minimum);
  lua_pushinteger(L, maximum);
  assert(lua_pcall(L, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_gettop(L) == base + 2);
  assert(lua_isnil(L, -2));
  assert(strcmp(lua_tostring(L, -1), expected) == 0);
  lua_settop(L, base);
}

static void test_span_boundaries(lua_State* L) {
  const uint32_t zero[] = {0u};
  const uint32_t maximum[] = {UINT32_MAX};

  set_values(maximum, 1u);
  assert(call_success(L, 7, 7) == 7);
  set_values(maximum, 1u);
  assert(call_success(L, 0, 1) == 1);
  set_values(maximum, 1u);
  assert(call_success(L, -5, 4) == 0);
  set_values(maximum, 1u);
  (void)call_success(L, -32768, 32767);
  set_values(maximum, 1u);
  (void)call_success(L, 0, (lua_Integer)UINT32_MAX - 1);

  set_values(maximum, 1u);
  assert(call_success(L, 0, (lua_Integer)UINT32_MAX) ==
         (lua_Integer)UINT32_MAX);
  set_values(zero, 1u);
  assert(call_success(L, INT32_MIN, (lua_Integer)INT32_MAX) == INT32_MIN);
  set_values(maximum, 1u);
  assert(call_success(L, INT32_MIN, (lua_Integer)INT32_MAX - 1) == INT32_MIN);
  set_values(maximum, 1u);
  (void)call_success(L, -1, (lua_Integer)UINT32_MAX - 2);

  call_failure(L, 0, (lua_Integer)UINT32_MAX + 1,
               "random range exceeds 32-bit entropy");
  call_failure(L, 0, (lua_Integer)UINT32_MAX + 2,
               "random range exceeds 32-bit entropy");
  call_failure(L, 0, (lua_Integer)UINT32_MAX * 2 + 1,
               "random range exceeds 32-bit entropy");
  call_failure(L, LUA_MININTEGER, LUA_MAXINTEGER,
               "random range exceeds 32-bit entropy");
}

static void test_integer_extremes(lua_State* L) {
  const uint32_t value[] = {123u};
  set_values(value, 1u);
  assert(call_success(L, LUA_MININTEGER, LUA_MININTEGER) == LUA_MININTEGER);
  set_values(value, 1u);
  assert(call_success(L, LUA_MAXINTEGER, LUA_MAXINTEGER) == LUA_MAXINTEGER);
  call_failure(L, 1, 0, "min_value must be <= max_value");
}

static void test_rejection_and_provider_failure(lua_State* L) {
  const uint32_t rejected_then_accepted[] = {5u, 16u};
  set_values(rejected_then_accepted, 2u);
  assert(call_success(L, 10, 19) == 16);
  assert(g_value_index == 2u);

  set_values(NULL, 0u);
  g_provider_fails = true;
  call_failure(L, 0, 1, "random hardware failed");
}

static void test_non_integer(lua_State* L) {
  int base = lua_gettop(L);
  (void)push_integer_function(L);
  lua_pushnumber(L, 1.5);
  lua_pushinteger(L, 2);
  assert(lua_pcall(L, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_isnil(L, -2));
  assert(strcmp(lua_tostring(L, -1),
                "random.integer expects integer min_value and max_value") == 0);
  lua_settop(L, base);
}

int main(void) {
  lua_State* L = luaL_newstate();
  assert(L != NULL);
  luaL_requiref(L, "random", luaopen_random, 1);
  lua_pop(L, 1);
  lua_random_set_u32_provider_for_test(fixed_provider);

  test_span_boundaries(L);
  test_integer_extremes(L);
  test_rejection_and_provider_failure(L);
  test_non_integer(L);

  lua_close(L);
  puts("lua_random_test: ok");
  return 0;
}
