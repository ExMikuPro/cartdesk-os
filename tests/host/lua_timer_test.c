#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cart_log.h"
#include "lauxlib.h"
#include "lua.h"
#include "lua_foundation.h"
#include "lua_vm.h"

int luaopen_timer(lua_State* L);
bool lua_timer_owner_create(lua_State* L, uint32_t id, uint32_t generation);
void lua_timer_owner_destroy(lua_State* L, uint32_t id, uint32_t generation);
void lua_timer_process(lua_State* L, uint32_t id, uint32_t generation,
                       uint64_t now_ms);

#define OWNER_ID 9u
#define OWNER_GENERATION 17u

static lua_State* g_vm;
static uint64_t g_now_ms;
static uint32_t g_callback_count;
static uint32_t g_error_count;

lua_State* lua_foundation_main_thread(lua_State* L) { return L; }

bool lua_foundation_same_vm(lua_State* left, lua_State* right) {
  return left == right;
}

bool lua_foundation_current(lua_State* L,
                            lua_foundation_owner_view_t* owner) {
  if (L != g_vm) return false;
  if (owner != NULL) {
    owner->vm = L;
    owner->owner_id = OWNER_ID;
    owner->generation = OWNER_GENERATION;
    owner->cart_id = UINT64_C(0x1234);
    owner->app_id = "timer-test";
  }
  return true;
}

void lua_foundation_owner_enter(lua_State* L, uint32_t id, uint32_t generation) {
  assert(L == g_vm && id == OWNER_ID && generation == OWNER_GENERATION);
}

void lua_foundation_owner_leave(void) {}
void lua_ui_owner_enter(lua_State* L, uint32_t id, uint32_t generation) {
  assert(L == g_vm && id == OWNER_ID && generation == OWNER_GENERATION);
}
void lua_ui_owner_leave(void) {}

uint64_t lua_foundation_platform_uptime_ms(void) { return g_now_ms; }

void CartLog_Write(cart_log_level_t level, const char* tag,
                   const char* message) {
  (void)level;
  (void)tag;
  (void)message;
}

void lua_vm_report_timer_error(uint32_t owner_id, uint32_t generation,
                               const char* app_id, const char* message) {
  assert(owner_id == OWNER_ID && generation == OWNER_GENERATION);
  assert(strcmp(app_id, "timer-test") == 0);
  assert(strstr(message, "intentional timer failure") != NULL);
  ++g_error_count;
}

static int successful_callback(lua_State* L) {
  (void)L;
  ++g_callback_count;
  return 0;
}

static int failing_callback(lua_State* L) {
  return luaL_error(L, "intentional timer failure");
}

static void create_and_cancel(lua_CFunction callback) {
  lua_settop(g_vm, 0);
  assert(luaopen_timer(g_vm) == 1);
  lua_getfield(g_vm, -1, "after");
  lua_pushinteger(g_vm, 5);
  lua_pushcfunction(g_vm, callback);
  assert(lua_pcall(g_vm, 2, 1, 0) == LUA_OK);
  assert(lua_isuserdata(g_vm, -1));
  lua_getfield(g_vm, 1, "cancel");
  lua_pushvalue(g_vm, 2);
  assert(lua_pcall(g_vm, 1, 1, 0) == LUA_OK && lua_toboolean(g_vm, -1));
}

static void create_timer(lua_CFunction callback, bool repeating) {
  lua_settop(g_vm, 0);
  assert(luaopen_timer(g_vm) == 1);
  lua_getfield(g_vm, -1, repeating ? "every" : "after");
  lua_pushinteger(g_vm, 5);
  lua_pushcfunction(g_vm, callback);
  assert(lua_pcall(g_vm, 2, 1, 0) == LUA_OK);
  assert(lua_isuserdata(g_vm, -1));
}

int main(void) {
  g_vm = luaL_newstate();
  assert(g_vm != NULL);
  assert(lua_timer_owner_create(g_vm, OWNER_ID, OWNER_GENERATION));

  for (uint32_t i = 0u; i < 1000u; ++i) {
    create_and_cancel(successful_callback);
  }
  assert(g_callback_count == 0u);

  for (uint32_t i = 0u; i < 32u; ++i) {
    create_timer(successful_callback, false);
  }
  lua_settop(g_vm, 0);
  assert(luaopen_timer(g_vm) == 1);
  lua_getfield(g_vm, -1, "after");
  lua_pushinteger(g_vm, 5);
  lua_pushcfunction(g_vm, successful_callback);
  assert(lua_pcall(g_vm, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_isnil(g_vm, -2));
  assert(strcmp(lua_tostring(g_vm, -1),
                "application timer limit reached") == 0);
  lua_timer_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);
  assert(lua_timer_owner_create(g_vm, OWNER_ID, OWNER_GENERATION));

  create_timer(successful_callback, false);
  g_now_ms = 5u;
  lua_timer_process(g_vm, OWNER_ID, OWNER_GENERATION, g_now_ms);
  assert(g_callback_count == 1u);

  create_timer(failing_callback, false);
  g_now_ms = 10u;
  lua_timer_process(g_vm, OWNER_ID, OWNER_GENERATION, g_now_ms);
  assert(g_error_count == 1u);

  lua_timer_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);
  lua_timer_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);
  lua_close(g_vm);
  puts("lua_timer_test: ok");
  return 0;
}
