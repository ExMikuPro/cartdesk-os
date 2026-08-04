#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lua_assets.h"
#include "lua_foundation.h"

int luaopen_assets(lua_State* L);
bool lua_assets_owner_create(lua_State* L, uint32_t id, uint32_t generation);
void lua_assets_owner_destroy(lua_State* L, uint32_t id, uint32_t generation);

#define OWNER_ID 21u
#define OWNER_GENERATION 34u

static lua_State* g_vm;
static bool g_resource_available = true;
static uint32_t g_acquire_count;
static uint32_t g_release_count;
static image_resource_t g_image;

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
    owner->app_id = "assets-test";
  }
  return true;
}

bool cart_path_is_valid(const char* path) {
  return path != NULL && path[0] != '/' && strstr(path, "..") == NULL;
}
bool cart_index_is_loaded(void) { return true; }
const cart_res_meta_t* cart_index_find(const char* path) {
  (void)path;
  return NULL;
}
bool cart_read_data(uint32_t offset, void* output, uint32_t size) {
  (void)offset;
  (void)output;
  (void)size;
  return false;
}

res_handle_t res_acquire_image(const char* path, res_lifetime_t lifetime) {
  (void)path;
  (void)lifetime;
  if (!g_resource_available) return (res_handle_t){0u, 0u};
  ++g_acquire_count;
  return (res_handle_t){1u, 1u};
}
bool res_handle_valid(res_handle_t handle) {
  return handle.index == 1u && handle.generation == 1u;
}
void res_release(res_handle_t handle) {
  assert(res_handle_valid(handle));
  ++g_release_count;
}
bool res_retain(res_handle_t handle) { return res_handle_valid(handle); }
const image_resource_t* res_get_image(res_handle_t handle) {
  return res_handle_valid(handle) ? &g_image : NULL;
}
const char* res_last_error(void) { return "injected asset load failure"; }

static void push_image_call(void) {
  assert(luaopen_assets(g_vm) == 1);
  lua_getfield(g_vm, -1, "image");
  lua_pushliteral(g_vm, "images/test.bin");
  assert(lua_pcall(g_vm, 1, LUA_MULTRET, 0) == LUA_OK);
}

int main(void) {
  g_vm = luaL_newstate();
  assert(g_vm != NULL);
  assert(lua_assets_owner_create(g_vm, OWNER_ID, OWNER_GENERATION));

  for (uint32_t i = 0u; i < 1000u; ++i) {
    push_image_call();
    assert(lua_isuserdata(g_vm, -1));
  }
  assert(g_acquire_count == 1000u && g_release_count == 0u);
  lua_assets_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);
  assert(g_release_count == 1000u);
  lua_assets_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);
  assert(g_release_count == 1000u);
  (void)lua_gc(g_vm, LUA_GCCOLLECT, 0);
  assert(g_release_count == 1000u);

  lua_settop(g_vm, 0);
  assert(lua_assets_owner_create(g_vm, OWNER_ID, OWNER_GENERATION));
  g_resource_available = false;
  push_image_call();
  assert(lua_isnil(g_vm, -2));
  assert(strcmp(lua_tostring(g_vm, -1), "injected asset load failure") == 0);
  assert(g_acquire_count == 1000u && g_release_count == 1000u);
  lua_assets_owner_destroy(g_vm, OWNER_ID, OWNER_GENERATION);

  lua_close(g_vm);
  puts("lua_assets_test: ok");
  return 0;
}
