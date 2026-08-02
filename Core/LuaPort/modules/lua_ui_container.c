#include "lua_ui.h"

#include <stdio.h>
#include <string.h>

typedef struct { lua_ui_handle_t handle; } lua_ui_container_t;

static const char* const k_create[] = {
    "id", "parent", "rect", "hidden", "enabled", "selected", "opacity"};
static const char* const k_patch[] = {
    "rect", "hidden", "enabled", "selected", "opacity"};

static bool apply(lua_State* L, lua_ui_container_t* container, int index,
                  bool creating, char* error, size_t error_size) {
  const char* id = NULL; bool present = false;
  const char* const* allowed = creating ? k_create : k_patch;
  size_t count = creating ? sizeof(k_create) / sizeof(k_create[0])
                          : sizeof(k_patch) / sizeof(k_patch[0]);
  if (!lua_ui_validate_properties(L, index, allowed, count,
                                  &container->handle, error, error_size) ||
      !lua_ui_read_optional_string(L, index, "id", &id, &present,
                                   error, error_size)) return false;
  if (present) {
    if (!id[0] || strlen(id) >= sizeof(container->handle.debug_id)) {
      (void)snprintf(error, error_size, "property 'id' is too long");
      return false;
    }
    (void)snprintf(container->handle.debug_id,
                   sizeof(container->handle.debug_id), "%s", id);
  }
  return lua_ui_apply_rect(L, index, container->handle.object, 0, 0,
                           creating ? 100 : 0, creating ? 100 : 0,
                           error, error_size) &&
         lua_ui_apply_hidden(L, index, container->handle.object,
                             error, error_size) &&
         lua_ui_apply_common_state(L, index, container->handle.object,
                                   error, error_size);
}

static int create(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (lua_gettop(L) != 2 || !lua_istable(L, 2))
    return lua_ui_push_error(L, "ui.container expects a properties table");
  lv_obj_t* parent = lua_ui_resolve_parent(L, 2, error, sizeof(error));
  if (!parent) return lua_ui_push_error(L, error);
  lua_ui_container_t* container = (lua_ui_container_t*)lua_ui_handle_new(
      L, sizeof(*container), LUA_UI_OBJECT_CONTAINER);
  if (!container) return lua_ui_push_error(L, "failed to allocate container handle");
  int handle_index = lua_gettop(L);
  lv_obj_t* object = lv_obj_create(parent);
  if (!object) return lua_ui_push_error(L, "failed to create container");
  container->handle.object = object;
  if (!apply(L, container, 2, true, error, sizeof(error)) ||
      !lua_ui_handle_register(L, handle_index, &container->handle, object, NULL)) {
    lv_obj_delete(object); container->handle.object = NULL;
    return lua_ui_push_error(L, error[0] ? error : "failed to register container owner");
  }
  return 1;
}

bool lua_ui_container_patch(lua_State* L, lua_ui_handle_t* handle,
                            int index, char* error, size_t error_size) {
  return handle && handle->object_type == LUA_UI_OBJECT_CONTAINER &&
      apply(L, (lua_ui_container_t*)handle, index, false, error, error_size);
}

int luaopen_ui_container(lua_State* L) {
  lua_newtable(L); lua_newtable(L); lua_pushcfunction(L, create);
  lua_setfield(L, -2, "__call"); lua_setmetatable(L, -2); return 1;
}
