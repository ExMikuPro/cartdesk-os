#include "lua.h"
#include "lauxlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cart_log.h"
#include "lua_foundation.h"
#include "lua_foundation_platform.h"
#include "lua_vm.h"
#include "lua_ui.h"

#define LUA_TIMER_HANDLE_MT "cartdesk.timer_handle"
#define LUA_TIMER_MAX_OWNERS 4u
#define LUA_TIMER_MAX_PER_OWNER 32u
#define LUA_TIMER_MIN_MS 5u
#define LUA_TIMER_MAX_MS (24u * 60u * 60u * 1000u)
#define LUA_TIMER_MAX_CALLBACKS_PER_FRAME 8u

typedef struct lua_timer_handle {
  lua_State* vm;
  uint32_t timer_id;
  uint32_t owner_id;
  uint32_t owner_generation;
  uint32_t generation;
  uint64_t deadline_ms;
  uint32_t interval_ms;
  int callback_ref;
  int self_ref;
  bool repeating;
  bool active;
  bool registered;
  struct lua_timer_handle* next;
} lua_timer_handle_t;

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  uint32_t next_timer_id;
  uint32_t count;
  lua_timer_handle_t* handles;
  bool active;
} lua_timer_owner_t;

static lua_timer_owner_t g_owners[LUA_TIMER_MAX_OWNERS];

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L); lua_pushstring(L, message); return 2;
}

static lua_timer_owner_t* find_owner(lua_State* L, uint32_t id, uint32_t gen) {
  L = lua_foundation_main_thread(L);
  for (size_t i = 0; i < LUA_TIMER_MAX_OWNERS; ++i) {
    if (g_owners[i].active && g_owners[i].vm == L &&
        g_owners[i].owner_id == id && g_owners[i].generation == gen)
      return &g_owners[i];
  }
  return NULL;
}

static lua_timer_handle_t* test_handle(lua_State* L, int index) {
  return (lua_timer_handle_t*)luaL_testudata(L, index, LUA_TIMER_HANDLE_MT);
}

static void detach(lua_timer_handle_t* handle) {
  if (!handle || !handle->registered) return;
  lua_timer_owner_t* owner = find_owner(handle->vm, handle->owner_id,
                                         handle->owner_generation);
  if (owner) {
    lua_timer_handle_t** cursor = &owner->handles;
    while (*cursor) {
      if (*cursor == handle) {
        *cursor = handle->next;
        if (owner->count > 0u) --owner->count;
        break;
      }
      cursor = &(*cursor)->next;
    }
  }
  handle->registered = false;
  handle->next = NULL;
}

static void deactivate(lua_timer_handle_t* handle) {
  if (!handle) return;
  lua_State* L = handle->vm;
  int callback_ref = handle->callback_ref;
  int self_ref = handle->self_ref;
  detach(handle);
  handle->active = false;
  handle->callback_ref = LUA_NOREF;
  handle->self_ref = LUA_NOREF;
  ++handle->generation;
  if (L && callback_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
  if (L && self_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, self_ref);
}

static int handle_gc(lua_State* L) {
  lua_timer_handle_t* handle = test_handle(L, 1);
  if (handle && handle->active) deactivate(handle);
  return 0;
}

static void ensure_metatable(lua_State* L) {
  if (luaL_newmetatable(L, LUA_TIMER_HANDLE_MT)) {
    lua_pushcfunction(L, handle_gc); lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "CartDesk timer handle"); lua_setfield(L, -2, "__name");
  }
  lua_pop(L, 1);
}

bool lua_timer_owner_create(lua_State* L, uint32_t id, uint32_t gen) {
  L = lua_foundation_main_thread(L);
  if (!L || find_owner(L, id, gen)) return false;
  for (size_t i = 0; i < LUA_TIMER_MAX_OWNERS; ++i) {
    if (!g_owners[i].active) {
      memset(&g_owners[i], 0, sizeof(g_owners[i]));
      g_owners[i].vm = L; g_owners[i].owner_id = id;
      g_owners[i].generation = gen; g_owners[i].next_timer_id = 1u;
      g_owners[i].active = true;
      return true;
    }
  }
  return false;
}

void lua_timer_owner_destroy(lua_State* L, uint32_t id, uint32_t gen) {
  lua_timer_owner_t* owner = find_owner(L, id, gen);
  if (!owner) return;
  while (owner->handles) deactivate(owner->handles);
  memset(owner, 0, sizeof(*owner));
}

static lua_timer_handle_t* validate(lua_State* L, int index,
                                    lua_timer_owner_t** out_owner,
                                    const char** error) {
  lua_timer_handle_t* handle = test_handle(L, index);
  if (!handle) { *error = "expected a CartDesk timer handle"; return NULL; }
  lua_foundation_owner_view_t current;
  if (!lua_foundation_current(L, &current) ||
      !lua_foundation_same_vm(handle->vm, L) ||
      handle->owner_id != current.owner_id ||
      handle->owner_generation != current.generation ||
      !find_owner(L, current.owner_id, current.generation)) {
    *error = "timer handle belongs to another application"; return NULL;
  }
  if (out_owner) *out_owner = find_owner(L, handle->owner_id, current.generation);
  return handle;
}

static int create_timer(lua_State* L, bool repeating) {
  if (lua_gettop(L) != 2 || !lua_isinteger(L, 1) || !lua_isfunction(L, 2))
    return fail(L, repeating ? "timer.every expects interval_ms and callback"
                             : "timer.after expects delay_ms and callback");
  lua_Integer delay = lua_tointeger(L, 1);
  if (delay < LUA_TIMER_MIN_MS || delay > LUA_TIMER_MAX_MS)
    return fail(L, "timer interval must be 5..86400000 ms");
  lua_foundation_owner_view_t current;
  if (!lua_foundation_current(L, &current))
    return fail(L, "timer requires an active application owner");
  lua_timer_owner_t* owner = find_owner(L, current.owner_id, current.generation);
  if (!owner || owner->count >= LUA_TIMER_MAX_PER_OWNER)
    return fail(L, "application timer limit reached");

  ensure_metatable(L);
  lua_timer_handle_t* handle =
      (lua_timer_handle_t*)lua_newuserdatauv(L, sizeof(*handle), 0);
  memset(handle, 0, sizeof(*handle));
  handle->vm = current.vm; handle->owner_id = current.owner_id;
  handle->owner_generation = current.generation;
  handle->generation = 1u;
  handle->timer_id = owner->next_timer_id++;
  handle->deadline_ms = lua_foundation_platform_uptime_ms() + (uint32_t)delay;
  handle->interval_ms = (uint32_t)delay;
  handle->repeating = repeating; handle->active = true; handle->registered = true;
  handle->callback_ref = LUA_NOREF; handle->self_ref = LUA_NOREF;
  luaL_getmetatable(L, LUA_TIMER_HANDLE_MT); lua_setmetatable(L, -2);
  lua_pushvalue(L, 2); handle->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushvalue(L, -1); handle->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  handle->next = owner->handles; owner->handles = handle; ++owner->count;
  return 1;
}

static int l_after(lua_State* L) { return create_timer(L, false); }
static int l_every(lua_State* L) { return create_timer(L, true); }

static int l_cancel(lua_State* L) {
  const char* error = NULL;
  if (lua_gettop(L) != 1) return fail(L, "timer.cancel expects a handle");
  lua_timer_handle_t* handle = validate(L, 1, NULL, &error);
  if (!handle) return fail(L, error);
  if (!handle->active) return fail(L, "timer is not active");
  deactivate(handle); lua_pushboolean(L, 1); return 1;
}

static int l_active(lua_State* L) {
  const char* error = NULL;
  if (lua_gettop(L) != 1) return fail(L, "timer.active expects a handle");
  lua_timer_handle_t* handle = validate(L, 1, NULL, &error);
  if (!handle) return fail(L, error);
  lua_pushboolean(L, handle->active); return 1;
}

static int l_now(lua_State* L) {
  if (lua_gettop(L) != 0) return fail(L, "timer.now_ms expects no arguments");
  uint64_t now = lua_foundation_platform_uptime_ms();
  if (now <= (uint64_t)LUA_MAXINTEGER) lua_pushinteger(L, (lua_Integer)now);
  else lua_pushnumber(L, (lua_Number)now);
  return 1;
}

static int traceback(lua_State* L) {
  const char* message = lua_tostring(L, 1);
  luaL_traceback(L, L, message ? message : "timer callback failed", 1);
  return 1;
}

void lua_timer_process(lua_State* L, uint32_t id, uint32_t gen, uint64_t now_ms) {
  lua_timer_owner_t* owner = find_owner(L, id, gen);
  if (!owner) return;
  uint32_t executed = 0u;
  while (executed < LUA_TIMER_MAX_CALLBACKS_PER_FRAME) {
    lua_timer_handle_t* handle = owner->handles;
    while (handle && (!handle->active || now_ms < handle->deadline_ms))
      handle = handle->next;
    if (!handle) break;
    {
      ++executed;
      int callback_ref = handle->callback_ref;
      bool repeating = handle->repeating;
      if (repeating) {
        do { handle->deadline_ms += handle->interval_ms; }
        while (handle->deadline_ms <= now_ms);
      } else {
        handle->active = false;
      }
      int base = lua_gettop(L);
      lua_pushcfunction(L, traceback);
      int error_index = lua_gettop(L);
      lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
      lua_foundation_owner_enter(L, id, gen);
      lua_ui_owner_enter(L, id, gen);
      int rc = lua_pcall(L, 0, 0, error_index);
      lua_ui_owner_leave();
      lua_foundation_owner_leave();
      if (rc != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        char error_message[LUA_RUNTIME_ERROR_MESSAGE_MAX];
        char app_id[LUA_FOUNDATION_APP_ID_MAX + 1u];
        (void)snprintf(error_message, sizeof(error_message), "%s",
                       message ? message : "timer callback failed");
        app_id[0] = '\0';
        lua_foundation_owner_enter(L, id, gen);
        lua_foundation_owner_view_t view;
        if (lua_foundation_current(L, &view)) {
          (void)snprintf(app_id, sizeof(app_id), "%s", view.app_id);
          CartLog_Write(CART_LOG_ERROR, view.app_id,
                        error_message);
        }
        lua_foundation_owner_leave();
        if (handle->registered) deactivate(handle);
        lua_settop(L, base);
        lua_vm_report_timer_error(id, gen, app_id, error_message);
        return;
      } else if (!repeating && handle->registered) {
        deactivate(handle);
      }
      lua_settop(L, base);
    }
  }
}

int luaopen_timer(lua_State* L) {
  static const luaL_Reg functions[] = {{"now_ms", l_now}, {"after", l_after},
      {"every", l_every}, {"cancel", l_cancel}, {"active", l_active},
      {NULL, NULL}};
  luaL_newlib(L, functions); return 1;
}
