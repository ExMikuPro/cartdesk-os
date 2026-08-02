#include "lua_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"

#ifndef LUA_UI_MAX_OWNERS
#define LUA_UI_MAX_OWNERS 4u
#endif

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  lv_obj_t* root;
  lua_ui_handle_t* root_handle;
  lua_ui_handle_t* handles;
  bool active;
  bool destroying;
} lua_ui_owner_t;

static lua_ui_owner_t g_owners[LUA_UI_MAX_OWNERS];
static lua_ui_owner_t* g_current_owner;

static void set_error(char* error, size_t error_size, const char* format, ...) {
  if (!error || error_size == 0u) return;
  va_list args;
  va_start(args, format);
  (void)vsnprintf(error, error_size, format, args);
  va_end(args);
}

static lua_ui_owner_t* find_owner(lua_State* L,
                                  uint32_t owner_id,
                                  uint32_t generation) {
  for (size_t i = 0; i < LUA_UI_MAX_OWNERS; ++i) {
    lua_ui_owner_t* owner = &g_owners[i];
    if (owner->active && owner->vm == L && owner->owner_id == owner_id &&
        owner->generation == generation) {
      return owner;
    }
  }
  return NULL;
}

static lua_ui_owner_t* find_free_owner(void) {
  for (size_t i = 0; i < LUA_UI_MAX_OWNERS; ++i) {
    if (!g_owners[i].active) return &g_owners[i];
  }
  return NULL;
}

static void detach_handle(lua_ui_handle_t* handle) {
  if (!handle || !handle->registered) return;
  lua_ui_owner_t* owner =
      find_owner(handle->vm, handle->owner_id, handle->owner_generation);
  if (owner) {
    lua_ui_handle_t** cursor = &owner->handles;
    while (*cursor) {
      if (*cursor == handle) {
        *cursor = handle->owner_next;
        break;
      }
      cursor = &(*cursor)->owner_next;
    }
  }
  handle->owner_next = NULL;
  handle->registered = false;
}

static void invalidate_handle(lua_ui_handle_t* handle) {
  if (!handle || !handle->alive) return;
  lua_State* L = handle->vm;
  int ref = handle->lua_ref;

  detach_handle(handle);
  handle->alive = false;
  handle->object = NULL;
  ++handle->generation;
  if (handle->cleanup) handle->cleanup(handle);
  handle->lua_ref = LUA_NOREF;
  if (L && ref != LUA_NOREF && ref != LUA_REFNIL) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
}

static void handle_delete_event_cb(lv_event_t* event) {
  lua_ui_handle_t* handle =
      (lua_ui_handle_t*)lv_event_get_user_data(event);
  invalidate_handle(handle);
}

static void owner_root_delete_event_cb(lv_event_t* event) {
  lua_ui_owner_t* owner = (lua_ui_owner_t*)lv_event_get_user_data(event);
  if (owner) {
    owner->root = NULL;
    owner->root_handle = NULL;
  }
}

static int handle_gc(lua_State* L) {
  lua_ui_handle_t* handle = lua_ui_handle_test(L, 1);
  if (handle) lua_ui_handle_delete(handle);
  return 0;
}

static void create_handle_metatable(lua_State* L) {
  if (luaL_newmetatable(L, LUA_UI_HANDLE_MT)) {
    lua_pushcfunction(L, handle_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "CartDesk UI handle");
    lua_setfield(L, -2, "__name");
  }
  lua_pop(L, 1);
}

void lua_ui_registry_init(void) {
  memset(g_owners, 0, sizeof(g_owners));
  g_current_owner = NULL;
}

bool lua_ui_owner_create(lua_State* L,
                         uint32_t owner_id,
                         uint32_t generation) {
  if (!L || owner_id == 0u || generation == 0u ||
      find_owner(L, owner_id, generation)) {
    return false;
  }

  lua_ui_owner_t* owner = find_free_owner();
  lv_obj_t* screen = lv_screen_active();
  if (!owner || !screen) return false;

  lv_obj_t* root = lv_obj_create(screen);
  if (!root) return false;
  lv_obj_remove_style_all(root);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  memset(owner, 0, sizeof(*owner));
  owner->vm = L;
  owner->owner_id = owner_id;
  owner->generation = generation;
  owner->root = root;
  owner->active = true;
  (void)lv_obj_add_event_cb(root, owner_root_delete_event_cb,
                            LV_EVENT_DELETE, owner);
  return true;
}

void lua_ui_owner_destroy(lua_State* L,
                          uint32_t owner_id,
                          uint32_t generation) {
  lua_ui_owner_t* owner = find_owner(L, owner_id, generation);
  if (!owner || owner->destroying) return;

  owner->destroying = true;
  if (g_current_owner == owner) g_current_owner = NULL;
  if (owner->root) {
    lv_obj_t* root = owner->root;
    owner->root = NULL;
    lv_obj_delete(root);
  }

  while (owner->handles) {
    lua_ui_handle_t* handle = owner->handles;
    if (handle->alive && handle->object) {
      lv_obj_delete(handle->object);
    } else {
      invalidate_handle(handle);
    }
  }
  memset(owner, 0, sizeof(*owner));
}

void lua_ui_owner_enter(lua_State* L,
                        uint32_t owner_id,
                        uint32_t generation) {
  g_current_owner = find_owner(L, owner_id, generation);
}

void lua_ui_owner_leave(void) {
  g_current_owner = NULL;
}

lv_obj_t* lua_ui_owner_root(lua_State* L) {
  (void)L;
  if (!g_current_owner || !g_current_owner->active ||
      g_current_owner->destroying) {
    return NULL;
  }
  return g_current_owner->root;
}

lua_ui_handle_t* lua_ui_handle_new(lua_State* L,
                                   size_t userdata_size,
                                   lua_ui_object_type_t object_type) {
  if (!L || userdata_size < sizeof(lua_ui_handle_t) ||
      !lua_ui_owner_root(L)) {
    return NULL;
  }
  create_handle_metatable(L);
  lua_ui_handle_t* handle =
      (lua_ui_handle_t*)lua_newuserdatauv(L, userdata_size, 0);
  memset(handle, 0, userdata_size);
  handle->vm = g_current_owner->vm;
  handle->owner_id = g_current_owner->owner_id;
  handle->owner_generation = g_current_owner->generation;
  handle->generation = 1u;
  handle->object_type = (uint16_t)object_type;
  handle->lua_ref = LUA_NOREF;
  luaL_getmetatable(L, LUA_UI_HANDLE_MT);
  lua_setmetatable(L, -2);
  return handle;
}

bool lua_ui_handle_register(lua_State* L,
                            int userdata_idx,
                            lua_ui_handle_t* handle,
                            lv_obj_t* object,
                            lua_ui_handle_cleanup_cb_t cleanup) {
  if (!L || !handle || !object || !g_current_owner ||
      handle->vm != g_current_owner->vm ||
      handle->owner_id != g_current_owner->owner_id ||
      handle->owner_generation != g_current_owner->generation) {
    return false;
  }

  userdata_idx = lua_absindex(L, userdata_idx);
  handle->object = object;
  handle->cleanup = cleanup;
  handle->alive = true;
  handle->registered = true;
  handle->owner_next = g_current_owner->handles;
  g_current_owner->handles = handle;
  (void)lv_obj_add_event_cb(object, handle_delete_event_cb,
                            LV_EVENT_DELETE, handle);
  lua_pushvalue(L, userdata_idx);
  handle->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return true;
}

void lua_ui_handle_delete(lua_ui_handle_t* handle) {
  if (!handle || !handle->alive) return;
  if (handle->object) {
    lv_obj_t* object = handle->object;
    lv_obj_delete(object);
  } else {
    invalidate_handle(handle);
  }
}

lua_ui_handle_t* lua_ui_handle_test(lua_State* L, int idx) {
  return (lua_ui_handle_t*)luaL_testudata(L, idx, LUA_UI_HANDLE_MT);
}

lua_ui_handle_t* lua_ui_handle_validate(lua_State* L,
                                        int idx,
                                        char* error,
                                        size_t error_size) {
  lua_ui_handle_t* handle = lua_ui_handle_test(L, idx);
  if (!handle) {
    set_error(error, error_size, "expected a CartDesk UI handle");
    return NULL;
  }
  if (!handle->alive || !handle->object) {
    set_error(error, error_size, "attempt to use a deleted UI object");
    return NULL;
  }
  if (!g_current_owner || handle->vm != g_current_owner->vm ||
      handle->owner_id != g_current_owner->owner_id ||
      handle->owner_generation != g_current_owner->generation ||
      !find_owner(handle->vm, handle->owner_id, handle->owner_generation)) {
    set_error(error, error_size, "UI handle belongs to another application");
    return NULL;
  }
  return handle;
}

const char* lua_ui_object_type_name(uint16_t object_type) {
  switch ((lua_ui_object_type_t)object_type) {
    case LUA_UI_OBJECT_ROOT: return "root";
    case LUA_UI_OBJECT_CONTAINER: return "container";
    case LUA_UI_OBJECT_LABEL: return "label";
    case LUA_UI_OBJECT_BUTTON: return "button";
    case LUA_UI_OBJECT_IMAGE: return "image";
    default: return "unknown";
  }
}

lv_obj_t* lua_ui_resolve_parent(lua_State* L,
                                int table_idx,
                                char* error,
                                size_t error_size) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, "parent");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lv_obj_t* root = lua_ui_owner_root(L);
    if (!root) set_error(error, error_size,
                         "UI creation requires an active application owner");
    return root;
  }
  lua_ui_handle_t* parent = lua_ui_handle_validate(L, -1, error, error_size);
  lua_pop(L, 1);
  if (!parent) return NULL;
  if (parent->object_type != LUA_UI_OBJECT_ROOT &&
      parent->object_type != LUA_UI_OBJECT_CONTAINER &&
      parent->object_type != LUA_UI_OBJECT_BUTTON) {
    set_error(error, error_size,
              "property 'parent' must be a root, container, or button handle");
    return NULL;
  }
  return parent->object;
}

static bool property_allowed(const char* property,
                             const char* const* allowed,
                             size_t allowed_count) {
  for (size_t i = 0; i < allowed_count; ++i) {
    if (strcmp(property, allowed[i]) == 0) return true;
  }
  return false;
}

bool lua_ui_validate_properties(lua_State* L,
                                int table_idx,
                                const char* const* allowed,
                                size_t allowed_count,
                                const lua_ui_handle_t* handle,
                                char* error,
                                size_t error_size) {
  if (!lua_istable(L, table_idx)) {
    set_error(error, error_size, "properties must be a table");
    return false;
  }
  table_idx = lua_absindex(L, table_idx);
  lua_pushnil(L);
  while (lua_next(L, table_idx) != 0) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      lua_pop(L, 2);
      set_error(error, error_size, "property names must be strings");
      return false;
    }
    const char* property = lua_tostring(L, -2);
    if (!property_allowed(property, allowed, allowed_count)) {
      lua_pop(L, 2);
      set_error(error, error_size,
                "ui.patch failed: object='%s', type='%s', property='%s' is unsupported",
                handle && handle->debug_id[0] ? handle->debug_id : "(unnamed)",
                handle ? lua_ui_object_type_name(handle->object_type) : "unknown",
                property);
      return false;
    }
    lua_pop(L, 1);
  }
  return true;
}

static bool read_rect_integer(lua_State* L,
                              int rect_idx,
                              lua_Integer index,
                              int32_t* value,
                              char* error,
                              size_t error_size) {
  lua_geti(L, rect_idx, index);
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    set_error(error, error_size, "property 'rect' expects four integers");
    return false;
  }
  lua_Integer raw = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (raw < INT32_MIN || raw > INT32_MAX) {
    set_error(error, error_size, "property 'rect' integer is out of range");
    return false;
  }
  *value = (int32_t)raw;
  return true;
}

bool lua_ui_apply_rect(lua_State* L,
                       int table_idx,
                       lv_obj_t* object,
                       int32_t default_x,
                       int32_t default_y,
                       int32_t default_w,
                       int32_t default_h,
                       char* error,
                       size_t error_size) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, "rect");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (default_w > 0 && default_h > 0) {
      lv_obj_set_pos(object, default_x, default_y);
      lv_obj_set_size(object, default_w, default_h);
    }
    return true;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    set_error(error, error_size, "property 'rect' expects a table");
    return false;
  }
  int rect_idx = lua_gettop(L);
  int32_t x, y, w, h;
  bool ok = read_rect_integer(L, rect_idx, 1, &x, error, error_size) &&
            read_rect_integer(L, rect_idx, 2, &y, error, error_size) &&
            read_rect_integer(L, rect_idx, 3, &w, error, error_size) &&
            read_rect_integer(L, rect_idx, 4, &h, error, error_size);
  lua_pop(L, 1);
  if (!ok) return false;
  if (w <= 0 || h <= 0) {
    set_error(error, error_size, "property 'rect' width and height must be positive");
    return false;
  }
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, w, h);
  return true;
}

bool lua_ui_apply_hidden(lua_State* L,
                         int table_idx,
                         lv_obj_t* object,
                         char* error,
                         size_t error_size) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, "hidden");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return true;
  }
  if (!lua_isboolean(L, -1)) {
    lua_pop(L, 1);
    set_error(error, error_size, "property 'hidden' expects boolean");
    return false;
  }
  bool hidden = lua_toboolean(L, -1);
  lua_pop(L, 1);
  if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  return true;
}

bool lua_ui_apply_common_state(lua_State* L,
                               int table_idx,
                               lv_obj_t* object,
                               char* error,
                               size_t error_size) {
  table_idx = lua_absindex(L, table_idx);
  const char* keys[] = {"enabled", "selected"};
  for (size_t i = 0; i < 2u; ++i) {
    lua_getfield(L, table_idx, keys[i]);
    if (!lua_isnil(L, -1)) {
      if (!lua_isboolean(L, -1)) {
        lua_pop(L, 1);
        set_error(error, error_size, "property '%s' expects boolean", keys[i]);
        return false;
      }
      bool value = lua_toboolean(L, -1);
      if (i == 0u) {
        if (value) lv_obj_remove_state(object, LV_STATE_DISABLED);
        else lv_obj_add_state(object, LV_STATE_DISABLED);
      } else {
        if (value) lv_obj_add_state(object, LV_STATE_CHECKED);
        else lv_obj_remove_state(object, LV_STATE_CHECKED);
      }
    }
    lua_pop(L, 1);
  }
  lua_getfield(L, table_idx, "opacity");
  if (!lua_isnil(L, -1)) {
    if (!lua_isinteger(L, -1)) {
      lua_pop(L, 1);
      set_error(error, error_size, "property 'opacity' expects integer");
      return false;
    }
    lua_Integer opacity = lua_tointeger(L, -1);
    if (opacity < 0 || opacity > 255) {
      lua_pop(L, 1);
      set_error(error, error_size, "property 'opacity' expects 0..255");
      return false;
    }
    lv_obj_set_style_opa(object, (lv_opa_t)opacity, 0);
  }
  lua_pop(L, 1);
  return true;
}

bool lua_ui_read_optional_string(lua_State* L,
                                 int table_idx,
                                 const char* key,
                                 const char** value,
                                 bool* present,
                                 char* error,
                                 size_t error_size) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (present) *present = false;
    return true;
  }
  if (!lua_isstring(L, -1)) {
    lua_pop(L, 1);
    set_error(error, error_size, "property '%s' expects string", key);
    return false;
  }
  if (value) *value = lua_tostring(L, -1);
  if (present) *present = true;
  lua_pop(L, 1);
  return true;
}

int lua_ui_push_error(lua_State* L, const char* error) {
  lua_pushnil(L);
  lua_pushstring(L, error ? error : "UI operation failed");
  return 2;
}

int lua_ui_push_object_error(lua_State* L,
                             const lua_ui_handle_t* handle,
                             const char* property,
                             const char* detail) {
  char error[LUA_UI_ERROR_MAX];
  (void)snprintf(error, sizeof(error),
                 "ui.patch failed: object='%s', type='%s', property='%s' %s",
                 handle && handle->debug_id[0] ? handle->debug_id : "(unnamed)",
                 handle ? lua_ui_object_type_name(handle->object_type) : "unknown",
                 property ? property : "(unknown)", detail ? detail : "is invalid");
  return lua_ui_push_error(L, error);
}

int lua_ui_patch(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (lua_gettop(L) != 2) {
    return lua_ui_push_error(L,
                             "ui.patch expects exactly handle and properties");
  }
  lua_ui_handle_t* handle =
      lua_ui_handle_validate(L, 1, error, sizeof(error));
  if (!handle) return lua_ui_push_error(L, error);
  if (!lua_istable(L, 2)) {
    return lua_ui_push_error(L, "properties must be a table");
  }

  bool ok = false;
  switch ((lua_ui_object_type_t)handle->object_type) {
    case LUA_UI_OBJECT_CONTAINER:
      ok = lua_ui_container_patch(L, handle, 2, error, sizeof(error));
      break;
    case LUA_UI_OBJECT_LABEL:
      ok = lua_ui_label_patch(L, handle, 2, error, sizeof(error));
      break;
    case LUA_UI_OBJECT_BUTTON:
      ok = lua_ui_button_patch(L, handle, 2, error, sizeof(error));
      break;
    case LUA_UI_OBJECT_IMAGE:
      ok = lua_ui_image_patch(L, handle, 2, error, sizeof(error));
      break;
    default:
      (void)snprintf(error, sizeof(error), "unsupported UI object type");
      break;
  }
  if (!ok) return lua_ui_push_error(L, error);
  lua_pushboolean(L, 1);
  return 1;
}

int lua_ui_root(lua_State* L) {
  if (lua_gettop(L) != 0)
    return lua_ui_push_error(L, "ui.root expects no arguments");
  if (!g_current_owner || !g_current_owner->root)
    return lua_ui_push_error(L, "ui.root requires an active application owner");
  if (g_current_owner->root_handle) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_current_owner->root_handle->lua_ref);
    return 1;
  }
  lua_ui_handle_t* handle = lua_ui_handle_new(
      L, sizeof(*handle), LUA_UI_OBJECT_ROOT);
  if (!handle) return lua_ui_push_error(L, "failed to allocate root handle");
  int index = lua_gettop(L);
  (void)snprintf(handle->debug_id, sizeof(handle->debug_id), "root");
  if (!lua_ui_handle_register(L, index, handle, g_current_owner->root, NULL))
    return lua_ui_push_error(L, "failed to register root handle");
  g_current_owner->root_handle = handle;
  return 1;
}

int lua_ui_delete(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (lua_gettop(L) != 1)
    return lua_ui_push_error(L, "ui.delete expects a handle");
  lua_ui_handle_t* handle = lua_ui_handle_validate(L, 1, error, sizeof(error));
  if (!handle) return lua_ui_push_error(L, error);
  if (handle->object_type == LUA_UI_OBJECT_ROOT)
    return lua_ui_push_error(L, "application root cannot be deleted");
  lua_ui_handle_delete(handle);
  lua_pushboolean(L, 1);
  return 1;
}
