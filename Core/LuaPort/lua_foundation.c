#include "lua_foundation.h"

#include <stdio.h>
#include <string.h>

#ifndef LUA_FOUNDATION_MAX_OWNERS
#define LUA_FOUNDATION_MAX_OWNERS 4u
#endif

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  uint64_t cart_id;
  char app_id[LUA_FOUNDATION_APP_ID_MAX + 1u];
  uint64_t log_window_ms;
  uint32_t log_count;
  uint32_t log_dropped;
  bool active;
} lua_foundation_owner_t;

bool lua_assets_owner_create(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_assets_owner_destroy(lua_State* L, uint32_t owner_id, uint32_t generation);
bool lua_storage_owner_create(lua_State* L, uint32_t owner_id, uint32_t generation,
                              uint64_t cart_id);
void lua_storage_owner_destroy(lua_State* L, uint32_t owner_id, uint32_t generation);
bool lua_timer_owner_create(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_timer_owner_destroy(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_timer_process(lua_State* L, uint32_t owner_id, uint32_t generation,
                       uint64_t now_ms);

static lua_foundation_owner_t g_owners[LUA_FOUNDATION_MAX_OWNERS];
static lua_foundation_owner_t* g_current_owner;

lua_State* lua_foundation_main_thread(lua_State* L) {
  if (!L) return NULL;
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  lua_State* main_thread = lua_tothread(L, -1);
  lua_settop(L, top);
  return main_thread ? main_thread : L;
}

bool lua_foundation_same_vm(lua_State* left, lua_State* right) {
  if (!left || !right) return false;
  return lua_foundation_main_thread(left) == lua_foundation_main_thread(right);
}

static lua_foundation_owner_t* find_owner(lua_State* L,
                                          uint32_t owner_id,
                                          uint32_t generation) {
  L = lua_foundation_main_thread(L);
  for (size_t i = 0; i < LUA_FOUNDATION_MAX_OWNERS; ++i) {
    lua_foundation_owner_t* owner = &g_owners[i];
    if (owner->active && owner->vm == L && owner->owner_id == owner_id &&
        owner->generation == generation) {
      return owner;
    }
  }
  return NULL;
}

void lua_foundation_registry_init(void) {
  memset(g_owners, 0, sizeof(g_owners));
  g_current_owner = NULL;
}

bool lua_foundation_owner_create(lua_State* L,
                                 uint32_t owner_id,
                                 uint32_t generation,
                                 uint64_t cart_id,
                                 const char* app_id) {
  L = lua_foundation_main_thread(L);
  if (!L || owner_id == 0u || generation == 0u ||
      find_owner(L, owner_id, generation)) {
    return false;
  }
  lua_foundation_owner_t* owner = NULL;
  for (size_t i = 0; i < LUA_FOUNDATION_MAX_OWNERS; ++i) {
    if (!g_owners[i].active) {
      owner = &g_owners[i];
      break;
    }
  }
  if (!owner) return false;

  memset(owner, 0, sizeof(*owner));
  owner->vm = L;
  owner->owner_id = owner_id;
  owner->generation = generation;
  owner->cart_id = cart_id;
  if (app_id && app_id[0] != '\0') {
    (void)snprintf(owner->app_id, sizeof(owner->app_id), "%s", app_id);
  } else {
    (void)snprintf(owner->app_id, sizeof(owner->app_id), "%08lX%08lX",
                   (unsigned long)(cart_id >> 32), (unsigned long)cart_id);
  }
  owner->active = true;

  if (!lua_assets_owner_create(L, owner_id, generation) ||
      !lua_timer_owner_create(L, owner_id, generation) ||
      !lua_storage_owner_create(L, owner_id, generation, cart_id)) {
    lua_timer_owner_destroy(L, owner_id, generation);
    lua_assets_owner_destroy(L, owner_id, generation);
    lua_storage_owner_destroy(L, owner_id, generation);
    memset(owner, 0, sizeof(*owner));
    return false;
  }
  return true;
}

void lua_foundation_owner_destroy(lua_State* L,
                                  uint32_t owner_id,
                                  uint32_t generation) {
  lua_foundation_owner_t* owner = find_owner(L, owner_id, generation);
  if (!owner) return;
  if (g_current_owner == owner) g_current_owner = NULL;
  lua_timer_owner_destroy(owner->vm, owner_id, generation);
  lua_assets_owner_destroy(owner->vm, owner_id, generation);
  lua_storage_owner_destroy(owner->vm, owner_id, generation);
  memset(owner, 0, sizeof(*owner));
}

void lua_foundation_owner_enter(lua_State* L,
                                uint32_t owner_id,
                                uint32_t generation) {
  g_current_owner = find_owner(L, owner_id, generation);
}

void lua_foundation_owner_leave(void) { g_current_owner = NULL; }

bool lua_foundation_current(lua_State* L,
                            lua_foundation_owner_view_t* out_owner) {
  if (!g_current_owner || !g_current_owner->active ||
      !lua_foundation_same_vm(g_current_owner->vm, L)) {
    return false;
  }
  if (out_owner) {
    out_owner->vm = g_current_owner->vm;
    out_owner->owner_id = g_current_owner->owner_id;
    out_owner->generation = g_current_owner->generation;
    out_owner->cart_id = g_current_owner->cart_id;
    out_owner->app_id = g_current_owner->app_id;
  }
  return true;
}

bool lua_foundation_log_allow(uint64_t now_ms, uint32_t* out_dropped) {
  if (!g_current_owner) return false;
  if (now_ms - g_current_owner->log_window_ms >= 1000u) {
    if (out_dropped) *out_dropped = g_current_owner->log_dropped;
    g_current_owner->log_window_ms = now_ms;
    g_current_owner->log_count = 0u;
    g_current_owner->log_dropped = 0u;
  } else if (out_dropped) {
    *out_dropped = 0u;
  }
  if (g_current_owner->log_count >= 32u) {
    ++g_current_owner->log_dropped;
    return false;
  }
  ++g_current_owner->log_count;
  return true;
}

void lua_foundation_process(lua_State* L, uint64_t now_ms) {
  lua_State* main_thread = lua_foundation_main_thread(L);
  for (size_t i = 0; i < LUA_FOUNDATION_MAX_OWNERS; ++i) {
    lua_foundation_owner_t* owner = &g_owners[i];
    if (!owner->active || owner->vm != main_thread) continue;
    lua_timer_process(main_thread, owner->owner_id, owner->generation, now_ms);
  }
}
