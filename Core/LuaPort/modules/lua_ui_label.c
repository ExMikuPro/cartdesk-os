#include "lua_ui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  lua_ui_handle_t handle;
} lua_ui_label_t;

static const char* const k_create_properties[] = {
    "id", "text", "rect", "hidden",
};
static const char* const k_patch_properties[] = {
    "text", "rect", "hidden",
};

static bool label_apply(lua_State* L,
                        lua_ui_label_t* label,
                        int properties_idx,
                        bool creating,
                        char* error,
                        size_t error_size) {
  const char* text = NULL;
  const char* id = NULL;
  bool text_present = false;
  bool id_present = false;
  const char* const* allowed = creating ? k_create_properties : k_patch_properties;
  size_t allowed_count = creating
      ? sizeof(k_create_properties) / sizeof(k_create_properties[0])
      : sizeof(k_patch_properties) / sizeof(k_patch_properties[0]);

  if (!lua_ui_validate_properties(L, properties_idx, allowed, allowed_count,
                                  &label->handle, error, error_size) ||
      !lua_ui_read_optional_string(L, properties_idx, "text", &text,
                                   &text_present, error, error_size) ||
      !lua_ui_read_optional_string(L, properties_idx, "id", &id,
                                   &id_present, error, error_size)) {
    return false;
  }
  if (id_present) {
    if (id[0] == '\0' || strlen(id) >= sizeof(label->handle.debug_id)) {
      (void)snprintf(error, error_size,
                     "property 'id' must be 1..%lu bytes",
                     (unsigned long)(sizeof(label->handle.debug_id) - 1u));
      return false;
    }
    (void)snprintf(label->handle.debug_id,
                   sizeof(label->handle.debug_id), "%s", id);
  }
  if (text_present) lv_label_set_text(label->handle.object, text);

  int32_t default_w = creating ? 100 : 0;
  int32_t default_h = creating ? 32 : 0;
  return lua_ui_apply_rect(L, properties_idx, label->handle.object,
                           0, 0, default_w, default_h, error, error_size) &&
         lua_ui_apply_hidden(L, properties_idx, label->handle.object,
                             error, error_size);
}

static int label_create(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (!lua_istable(L, 2)) {
    return lua_ui_push_error(L, "ui.label expects a properties table");
  }
  lv_obj_t* root = lua_ui_owner_root(L);
  if (!root) {
    return lua_ui_push_error(L, "ui.label requires an active application owner");
  }

  lua_ui_label_t* label = (lua_ui_label_t*)lua_ui_handle_new(
      L, sizeof(*label), LUA_UI_OBJECT_LABEL);
  if (!label) return lua_ui_push_error(L, "failed to allocate label handle");
  const int handle_idx = lua_gettop(L);
  lv_obj_t* object = lv_label_create(root);
  if (!object) return lua_ui_push_error(L, "failed to create label");
  label->handle.object = object;
  lv_label_set_text(object, "");

  if (!label_apply(L, label, 2, true, error, sizeof(error))) {
    lv_obj_delete(object);
    label->handle.object = NULL;
    return lua_ui_push_error(L, error);
  }
  if (!lua_ui_handle_register(L, handle_idx, &label->handle, object, NULL)) {
    lv_obj_delete(object);
    label->handle.object = NULL;
    return lua_ui_push_error(L, "failed to register label owner");
  }
  return 1;
}

bool lua_ui_label_patch(lua_State* L,
                        lua_ui_handle_t* handle,
                        int properties_idx,
                        char* error,
                        size_t error_size) {
  if (!handle || handle->object_type != LUA_UI_OBJECT_LABEL) return false;
  return label_apply(L, (lua_ui_label_t*)handle, properties_idx, false,
                     error, error_size);
}

int luaopen_ui_label(lua_State* L) {
  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, label_create);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
