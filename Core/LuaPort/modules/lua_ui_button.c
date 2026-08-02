#include "lua_ui.h"

#include <stdio.h>
#include <string.h>

#include "lua_vm.h"

typedef struct {
  lua_ui_handle_t handle;
  lv_obj_t* label;
  char input_id[LUA_INPUT_ACTION_ID_MAX];
} lua_ui_button_t;

static const char* const k_create_properties[] = {
    "id", "parent", "text", "rect", "hidden", "input", "style",
    "enabled", "selected", "opacity",
};
static const char* const k_patch_properties[] = {
    "text", "rect", "hidden", "style", "enabled", "selected", "opacity",
};
static const char* const k_style_properties[] = {
    "bg", "bg_alpha", "text", "border", "radius",
};
static const char* const k_border_properties[] = {
    "color", "width",
};

static lua_ui_button_t* as_button(lua_ui_handle_t* handle) {
  return (lua_ui_button_t*)handle;
}

static void button_cleanup(lua_ui_handle_t* handle) {
  lua_ui_button_t* button = as_button(handle);
  button->label = NULL;
  button->input_id[0] = '\0';
}

static bool style_integer(lua_State* L,
                          int table_idx,
                          const char* key,
                          int32_t* value,
                          bool* present,
                          char* error,
                          size_t error_size) {
  lua_getfield(L, table_idx, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    *present = false;
    return true;
  }
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size,
                   "property 'style.%s' expects integer", key);
    return false;
  }
  lua_Integer raw = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (raw < 0 || raw > INT32_MAX) {
    (void)snprintf(error, error_size,
                   "property 'style.%s' is out of range", key);
    return false;
  }
  *value = (int32_t)raw;
  *present = true;
  return true;
}

static bool button_apply_style(lua_State* L,
                               lua_ui_button_t* button,
                               int properties_idx,
                               char* error,
                               size_t error_size) {
  properties_idx = lua_absindex(L, properties_idx);
  lua_getfield(L, properties_idx, "style");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return true;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size, "property 'style' expects a table");
    return false;
  }
  int style_idx = lua_gettop(L);
  if (!lua_ui_validate_properties(L, style_idx, k_style_properties,
                                  sizeof(k_style_properties) /
                                      sizeof(k_style_properties[0]),
                                  &button->handle, error, error_size)) {
    lua_pop(L, 1);
    return false;
  }

  int32_t value;
  bool present;
  if (!style_integer(L, style_idx, "bg", &value, &present,
                     error, error_size)) goto fail;
  if (present) lv_obj_set_style_bg_color(button->handle.object,
                                         lv_color_hex((uint32_t)value), 0);
  if (!style_integer(L, style_idx, "bg_alpha", &value, &present,
                     error, error_size)) goto fail;
  if (present) {
    if (value > 255) {
      (void)snprintf(error, error_size,
                     "property 'style.bg_alpha' expects 0..255");
      goto fail;
    }
    lv_obj_set_style_bg_opa(button->handle.object, (lv_opa_t)value, 0);
  }
  if (!style_integer(L, style_idx, "text", &value, &present,
                     error, error_size)) goto fail;
  if (present) lv_obj_set_style_text_color(button->label,
                                           lv_color_hex((uint32_t)value), 0);
  if (!style_integer(L, style_idx, "radius", &value, &present,
                     error, error_size)) goto fail;
  if (present) lv_obj_set_style_radius(button->handle.object, value, 0);

  lua_getfield(L, style_idx, "border");
  if (!lua_isnil(L, -1)) {
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      (void)snprintf(error, error_size,
                     "property 'style.border' expects a table");
      goto fail;
    }
    int border_idx = lua_gettop(L);
    if (!lua_ui_validate_properties(L, border_idx, k_border_properties,
                                    sizeof(k_border_properties) /
                                        sizeof(k_border_properties[0]),
                                    &button->handle, error, error_size)) {
      lua_pop(L, 1);
      goto fail;
    }
    if (!style_integer(L, border_idx, "color", &value, &present,
                       error, error_size)) {
      lua_pop(L, 1);
      goto fail;
    }
    if (present) lv_obj_set_style_border_color(button->handle.object,
                                                lv_color_hex((uint32_t)value), 0);
    if (!style_integer(L, border_idx, "width", &value, &present,
                       error, error_size)) {
      lua_pop(L, 1);
      goto fail;
    }
    if (present) lv_obj_set_style_border_width(button->handle.object, value, 0);
  }
  lua_pop(L, 1);
  lua_pop(L, 1);
  return true;

fail:
  lua_pop(L, 1);
  return false;
}

static bool button_apply(lua_State* L,
                         lua_ui_button_t* button,
                         int properties_idx,
                         bool creating,
                         char* error,
                         size_t error_size) {
  const char* text = NULL;
  const char* id = NULL;
  const char* input = NULL;
  bool text_present = false;
  bool id_present = false;
  bool input_present = false;
  const char* const* allowed = creating ? k_create_properties : k_patch_properties;
  size_t allowed_count = creating
      ? sizeof(k_create_properties) / sizeof(k_create_properties[0])
      : sizeof(k_patch_properties) / sizeof(k_patch_properties[0]);

  if (!lua_ui_validate_properties(L, properties_idx, allowed, allowed_count,
                                  &button->handle, error, error_size) ||
      !lua_ui_read_optional_string(L, properties_idx, "text", &text,
                                   &text_present, error, error_size) ||
      !lua_ui_read_optional_string(L, properties_idx, "id", &id,
                                   &id_present, error, error_size) ||
      !lua_ui_read_optional_string(L, properties_idx, "input", &input,
                                   &input_present, error, error_size)) {
    return false;
  }

  if (id_present) {
    if (id[0] == '\0' || strlen(id) >= sizeof(button->handle.debug_id)) {
      (void)snprintf(error, error_size,
                     "property 'id' must be 1..%lu bytes",
                     (unsigned long)(sizeof(button->handle.debug_id) - 1u));
      return false;
    }
    (void)snprintf(button->handle.debug_id,
                   sizeof(button->handle.debug_id), "%s", id);
  }
  if (input_present) {
    if (input[0] == '\0' || strlen(input) >= sizeof(button->input_id)) {
      (void)snprintf(error, error_size,
                     "property 'input' must be 1..%lu bytes",
                     (unsigned long)(sizeof(button->input_id) - 1u));
      return false;
    }
    (void)snprintf(button->input_id, sizeof(button->input_id), "%s", input);
  }
  if (text_present) lv_label_set_text(button->label, text);

  int32_t default_w = creating ? 100 : 0;
  int32_t default_h = creating ? 50 : 0;
  if (!lua_ui_apply_rect(L, properties_idx, button->handle.object,
                         0, 0, default_w, default_h, error, error_size) ||
      !lua_ui_apply_hidden(L, properties_idx, button->handle.object,
                           error, error_size) ||
      !lua_ui_apply_common_state(L, properties_idx, button->handle.object,
                                 error, error_size)) {
    return false;
  }
  if (!button_apply_style(L, button, properties_idx, error, error_size)) {
    return false;
  }
  lv_obj_center(button->label);
  return true;
}

static const char* button_event_name(lv_event_code_t code) {
  switch (code) {
    case LV_EVENT_CLICKED: return "clicked";
    case LV_EVENT_PRESSED: return "pressed";
    case LV_EVENT_RELEASED: return "released";
    default: return NULL;
  }
}

static void button_event_cb(lv_event_t* event) {
  lua_ui_button_t* button =
      (lua_ui_button_t*)lv_event_get_user_data(event);
  const char* name = button_event_name(lv_event_get_code(event));
  if (!button || !button->handle.alive || !name ||
      button->input_id[0] == '\0') {
    return;
  }

  LuaInputAction action = {0};
  (void)snprintf(action.event, sizeof(action.event), "%s", name);
  action.pressed = lv_event_get_code(event) == LV_EVENT_PRESSED ||
                   lv_event_get_code(event) == LV_EVENT_CLICKED;
  action.released = lv_event_get_code(event) == LV_EVENT_RELEASED ||
                    lv_event_get_code(event) == LV_EVENT_CLICKED;
  action.value = action.pressed ? 1.0f : 0.0f;
  (void)lua_post_input_for_owner(button->handle.owner_id,
                                 button->handle.owner_generation,
                                 button->input_id, &action);
}

static int button_create(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (!lua_istable(L, 2)) {
    return lua_ui_push_error(L, "ui.button expects a properties table");
  }
  lv_obj_t* root = lua_ui_resolve_parent(L, 2, error, sizeof(error));
  if (!root) return lua_ui_push_error(L, error);

  lua_ui_button_t* button = (lua_ui_button_t*)lua_ui_handle_new(
      L, sizeof(*button), LUA_UI_OBJECT_BUTTON);
  if (!button) return lua_ui_push_error(L, "failed to allocate button handle");
  const int handle_idx = lua_gettop(L);

  lv_obj_t* object = lv_button_create(root);
  if (!object) return lua_ui_push_error(L, "failed to create button");
  button->handle.object = object;
  button->label = lv_label_create(object);
  if (!button->label) {
    lv_obj_delete(object);
    button->handle.object = NULL;
    return lua_ui_push_error(L, "failed to create button label");
  }
  lv_label_set_text(button->label, "");

  if (!button_apply(L, button, 2, true, error, sizeof(error))) {
    lv_obj_delete(object);
    button->handle.object = NULL;
    button->label = NULL;
    return lua_ui_push_error(L, error);
  }
  if (!lua_ui_handle_register(L, handle_idx, &button->handle, object,
                              button_cleanup)) {
    lv_obj_delete(object);
    button->handle.object = NULL;
    button->label = NULL;
    return lua_ui_push_error(L, "failed to register button owner");
  }
  (void)lv_obj_add_event_cb(object, button_event_cb, LV_EVENT_ALL, button);
  return 1;
}

bool lua_ui_button_patch(lua_State* L,
                         lua_ui_handle_t* handle,
                         int properties_idx,
                         char* error,
                         size_t error_size) {
  if (!handle || handle->object_type != LUA_UI_OBJECT_BUTTON) return false;
  return button_apply(L, as_button(handle), properties_idx, false,
                      error, error_size);
}

int luaopen_ui_button(lua_State* L) {
  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, button_create);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
