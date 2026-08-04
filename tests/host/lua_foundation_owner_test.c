#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "lua_foundation.h"

static lua_State* g_expected_vm;
static unsigned g_destroy_count;
static uint32_t g_expected_owner_id = 7u;
static uint32_t g_expected_generation = 11u;
static uint64_t g_expected_cart_id = UINT64_C(0x1122334455667788);

bool lua_assets_owner_create(lua_State* L, uint32_t id, uint32_t generation) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  return true;
}

void lua_assets_owner_destroy(lua_State* L, uint32_t id,
                              uint32_t generation) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  ++g_destroy_count;
}

bool lua_storage_owner_create(lua_State* L, uint32_t id, uint32_t generation,
                              uint64_t cart_id) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  assert(cart_id == g_expected_cart_id);
  return true;
}

void lua_storage_owner_destroy(lua_State* L, uint32_t id,
                               uint32_t generation) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  ++g_destroy_count;
}

bool lua_storage_handle_io_completion(const cart_io_completion_t* completion) {
  (void)completion;
  return false;
}

bool lua_storage_all_ready(void) { return true; }

bool lua_timer_owner_create(lua_State* L, uint32_t id, uint32_t generation) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  return true;
}

void lua_timer_owner_destroy(lua_State* L, uint32_t id,
                             uint32_t generation) {
  assert(L == g_expected_vm && id == g_expected_owner_id &&
         generation == g_expected_generation);
  ++g_destroy_count;
}

void lua_timer_process(lua_State* L, uint32_t id, uint32_t generation,
                       uint64_t now_ms) {
  (void)L;
  (void)id;
  (void)generation;
  (void)now_ms;
}

int main(void) {
  lua_State* L = luaL_newstate();
  lua_State* other = luaL_newstate();
  assert(L && other);
  g_expected_vm = L;

  lua_foundation_registry_init();
  assert(lua_foundation_owner_create(L, 7u, 11u,
                                     UINT64_C(0x1122334455667788),
                                     "owner-test"));

  lua_State* coroutine = lua_newthread(L);
  assert(coroutine);
  assert(lua_foundation_main_thread(coroutine) == L);
  assert(lua_foundation_same_vm(L, coroutine));
  assert(!lua_foundation_same_vm(L, other));

  lua_foundation_owner_enter(coroutine, 7u, 11u);
  lua_foundation_owner_view_t owner;
  assert(lua_foundation_current(coroutine, &owner));
  assert(owner.vm == L);
  assert(owner.owner_id == 7u && owner.generation == 11u);
  assert(!lua_foundation_current(other, NULL));
  lua_foundation_owner_leave();

  lua_foundation_owner_destroy(coroutine, 7u, 11u);
  assert(g_destroy_count == 3u);
  lua_foundation_owner_enter(L, 7u, 11u);
  assert(!lua_foundation_current(L, NULL));

  for (uint32_t i = 0u; i < 100u; ++i) {
    g_expected_owner_id = 100u + i;
    g_expected_generation = 200u + i;
    g_expected_cart_id = UINT64_C(0xA000000000000000) + i;
    assert(lua_foundation_owner_create(L, g_expected_owner_id,
                                       g_expected_generation,
                                       g_expected_cart_id, "stress-owner"));
    lua_foundation_owner_enter(L, g_expected_owner_id,
                               g_expected_generation);
    lua_foundation_owner_view_t stress_owner;
    assert(lua_foundation_current(L, &stress_owner));
    assert(stress_owner.owner_id == g_expected_owner_id);
    lua_foundation_owner_leave();
    lua_foundation_owner_destroy(L, g_expected_owner_id,
                                 g_expected_generation);
    unsigned expected_destroy_count = 3u + (i + 1u) * 3u;
    assert(g_destroy_count == expected_destroy_count);
    lua_foundation_owner_destroy(L, g_expected_owner_id,
                                 g_expected_generation);
    assert(g_destroy_count == expected_destroy_count);
  }

  lua_close(other);
  lua_close(L);
  puts("lua_foundation_owner_test: ok");
  return 0;
}
