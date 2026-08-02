#include "lua_assets.h"

#include <string.h>

#include "cart_index.h"
#include "lauxlib.h"
#include "lua_foundation.h"
#include "xhgc_cart.h"

#define LUA_ASSETS_MAX_OWNERS 4u
#define LUA_ASSETS_DATA_MAX (256u * 1024u)

typedef struct lua_asset_handle {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t owner_generation;
  uint32_t generation;
  uint32_t resource_type;
  res_handle_t resource;
  int self_ref;
  bool alive;
  bool registered;
  struct lua_asset_handle* next;
} lua_asset_handle_t;

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  lua_asset_handle_t* handles;
  bool active;
} lua_asset_owner_t;

static lua_asset_owner_t g_owners[LUA_ASSETS_MAX_OWNERS];

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L); lua_pushstring(L, message); return 2;
}

static lua_asset_owner_t* find_owner(lua_State* L, uint32_t id, uint32_t gen) {
  L = lua_foundation_main_thread(L);
  for (size_t i = 0; i < LUA_ASSETS_MAX_OWNERS; ++i) {
    if (g_owners[i].active && g_owners[i].vm == L &&
        g_owners[i].owner_id == id && g_owners[i].generation == gen)
      return &g_owners[i];
  }
  return NULL;
}

static void invalidate(lua_asset_handle_t* handle) {
  if (!handle || !handle->alive) return;
  lua_asset_owner_t* owner = find_owner(handle->vm, handle->owner_id,
                                         handle->owner_generation);
  if (owner && handle->registered) {
    lua_asset_handle_t** cursor = &owner->handles;
    while (*cursor) {
      if (*cursor == handle) { *cursor = handle->next; break; }
      cursor = &(*cursor)->next;
    }
  }
  if (res_handle_valid(handle->resource)) res_release(handle->resource);
  int self_ref = handle->self_ref;
  handle->alive = false; handle->registered = false; handle->next = NULL;
  handle->self_ref = LUA_NOREF; ++handle->generation;
  if (handle->vm && self_ref != LUA_NOREF)
    luaL_unref(handle->vm, LUA_REGISTRYINDEX, self_ref);
}

static int handle_gc(lua_State* L) {
  lua_asset_handle_t* handle =
      (lua_asset_handle_t*)luaL_testudata(L, 1, LUA_ASSET_HANDLE_MT);
  if (handle) invalidate(handle);
  return 0;
}

static void ensure_metatable(lua_State* L) {
  if (luaL_newmetatable(L, LUA_ASSET_HANDLE_MT)) {
    lua_pushcfunction(L, handle_gc); lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "CartDesk asset handle"); lua_setfield(L, -2, "__name");
  }
  lua_pop(L, 1);
}

bool lua_assets_owner_create(lua_State* L, uint32_t id, uint32_t gen) {
  L = lua_foundation_main_thread(L);
  if (!L || find_owner(L, id, gen)) return false;
  for (size_t i = 0; i < LUA_ASSETS_MAX_OWNERS; ++i) {
    if (!g_owners[i].active) {
      memset(&g_owners[i], 0, sizeof(g_owners[i]));
      g_owners[i].vm = L; g_owners[i].owner_id = id;
      g_owners[i].generation = gen; g_owners[i].active = true;
      return true;
    }
  }
  return false;
}

void lua_assets_owner_destroy(lua_State* L, uint32_t id, uint32_t gen) {
  lua_asset_owner_t* owner = find_owner(L, id, gen);
  if (!owner) return;
  while (owner->handles) invalidate(owner->handles);
  memset(owner, 0, sizeof(*owner));
}

static bool read_path(lua_State* L, int index, const char** path,
                      const char** error) {
  if (lua_type(L, index) != LUA_TSTRING) {
    *error = "asset path must be a string"; return false;
  }
  size_t length = 0u;
  *path = lua_tolstring(L, index, &length);
  if (length == 0u || length >= 256u || !cart_path_is_valid(*path)) {
    *error = "asset path is invalid"; return false;
  }
  return true;
}

static int l_exists(lua_State* L) {
  const char* path = NULL; const char* error = NULL;
  if (lua_gettop(L) != 1 || !read_path(L, 1, &path, &error)) return fail(L, error);
  if (!lua_foundation_current(L, NULL))
    return fail(L, "assets.exists requires an active application owner");
  if (!cart_index_is_loaded()) return fail(L, "cart resource index is unavailable");
  lua_pushboolean(L, cart_index_find(path) != NULL); return 1;
}

static int l_image(lua_State* L) {
  const char* path = NULL; const char* error = NULL;
  if (lua_gettop(L) != 1 || !read_path(L, 1, &path, &error)) return fail(L, error);
  lua_foundation_owner_view_t current;
  if (!lua_foundation_current(L, &current))
    return fail(L, "assets.image requires an active application owner");
  lua_asset_owner_t* owner = find_owner(L, current.owner_id, current.generation);
  if (!owner) return fail(L, "asset owner is unavailable");
  res_handle_t resource = res_acquire_image(path, RES_LIFE_SCENE);
  if (!res_handle_valid(resource))
    return fail(L, res_last_error() ? res_last_error() : "image load failed");
  ensure_metatable(L);
  lua_asset_handle_t* handle =
      (lua_asset_handle_t*)lua_newuserdatauv(L, sizeof(*handle), 0);
  memset(handle, 0, sizeof(*handle));
  handle->vm = current.vm; handle->owner_id = current.owner_id;
  handle->owner_generation = current.generation; handle->generation = 1u;
  handle->resource_type = XHGC_RES_IMAGE; handle->resource = resource;
  handle->alive = true; handle->registered = true; handle->self_ref = LUA_NOREF;
  luaL_getmetatable(L, LUA_ASSET_HANDLE_MT); lua_setmetatable(L, -2);
  lua_pushvalue(L, -1); handle->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  handle->next = owner->handles; owner->handles = handle;
  return 1;
}

static int l_data(lua_State* L) {
  const char* path = NULL; const char* error = NULL;
  if (lua_gettop(L) != 1 || !read_path(L, 1, &path, &error)) return fail(L, error);
  if (!lua_foundation_current(L, NULL))
    return fail(L, "assets.data requires an active application owner");
  if (!cart_index_is_loaded()) return fail(L, "cart resource index is unavailable");
  const cart_res_meta_t* meta = cart_index_find(path);
  if (!meta) return fail(L, "resource not found");
  if (meta->size > LUA_ASSETS_DATA_MAX) return fail(L, "resource exceeds 256 KiB limit");
  int base = lua_gettop(L);
  luaL_Buffer buffer;
  char* bytes = luaL_buffinitsize(L, &buffer, meta->size);
  if (!cart_read_data(meta->data_off, bytes, meta->size)) {
    lua_settop(L, base); return fail(L, "resource read failed");
  }
  luaL_pushresultsize(&buffer, meta->size); return 1;
}

bool lua_asset_image_acquire(lua_State* L, int index, res_handle_t* out_handle,
                             const image_resource_t** out_image,
                             const char** out_error) {
  lua_asset_handle_t* handle =
      (lua_asset_handle_t*)luaL_testudata(L, index, LUA_ASSET_HANDLE_MT);
  lua_foundation_owner_view_t current;
  if (!handle) { *out_error = "src expects a path or image asset handle"; return false; }
  if (!handle->alive || !res_handle_valid(handle->resource)) {
    *out_error = "attempt to use a deleted asset"; return false;
  }
  if (!lua_foundation_current(L, &current) ||
      !lua_foundation_same_vm(handle->vm, L) ||
      handle->owner_id != current.owner_id ||
      handle->owner_generation != current.generation) {
    *out_error = "asset handle belongs to another application"; return false;
  }
  if (handle->resource_type != XHGC_RES_IMAGE || !res_retain(handle->resource)) {
    *out_error = "asset is not an available image"; return false;
  }
  const image_resource_t* image = res_get_image(handle->resource);
  if (!image) { res_release(handle->resource); *out_error = "image asset is unavailable"; return false; }
  *out_handle = handle->resource; *out_image = image; return true;
}

int luaopen_assets(lua_State* L) {
  static const luaL_Reg functions[] = {{"exists", l_exists}, {"image", l_image},
                                        {"data", l_data}, {NULL, NULL}};
  luaL_newlib(L, functions); return 1;
}
