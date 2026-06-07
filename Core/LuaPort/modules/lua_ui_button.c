#include "lua_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua_vm.h"
#include "lvgl.h"

typedef struct {
  lv_obj_t* btn;
  lv_obj_t* label;
  lv_event_dsc_t* event_dsc;
  lua_State* L;
  char id[LUA_UI_DRAWABLE_ID_MAX];
  char input_id[LUA_INPUT_ACTION_ID_MAX];
} lvgl_btn_ud_t;

#define UI_BUTTON_MT "ui.button.mt"

static lvgl_btn_ud_t* test_btn(lua_State* L, int idx) {
  return (lvgl_btn_ud_t*)luaL_testudata(L, idx, UI_BUTTON_MT);
}

static lvgl_btn_ud_t* check_btn(lua_State* L, int idx) {
  return (lvgl_btn_ud_t*)luaL_checkudata(L, idx, UI_BUTTON_MT);
}

static void check_btn_valid(lua_State* L, lvgl_btn_ud_t* ud) {
  if (!ud || !ud->btn) {
    luaL_error(L, "button has been deleted");
  }
}

static bool table_get_int(lua_State* L, int table, const char* key, int32_t* out) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = (int32_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool table_get_uint(lua_State* L, int table, const char* key, uint32_t* out) {
  int32_t value = 0;
  if (!table_get_int(L, table, key, &value)) return false;
  luaL_argcheck(L, value >= 0, 1, "color must be non-negative");
  *out = (uint32_t)value;
  return true;
}

static bool table_get_int_index(lua_State* L, int table, lua_Integer index, int32_t* out) {
  table = lua_absindex(L, table);
  lua_geti(L, table, index);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = (int32_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool table_get_string(lua_State* L, int table, const char* key, const char** out) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = luaL_checkstring(L, -1);
  lua_pop(L, 1);
  return true;
}

static void btn_set_input_id(lua_State* L, lvgl_btn_ud_t* ud, const char* input_id) {
  luaL_argcheck(L, input_id && input_id[0] != '\0', 1, "input must not be empty");
  luaL_argcheck(L, strlen(input_id) < LUA_INPUT_ACTION_ID_MAX, 1, "input is too long");
  snprintf(ud->input_id, sizeof(ud->input_id), "%s", input_id);
}

static const char* btn_event_string(lv_event_code_t code) {
  switch (code) {
    case LV_EVENT_CLICKED: return "clicked";
    case LV_EVENT_PRESSED: return "pressed";
    case LV_EVENT_RELEASED: return "released";
    default: return NULL;
  }
}

static void btn_post_input(lvgl_btn_ud_t* ud, lv_event_code_t code) {
  const char* event = btn_event_string(code);
  if (!ud || !event) return;

  LuaInputAction action = {0};
  snprintf(action.event, sizeof(action.event), "%s", event);
  action.pressed = (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED);
  action.released = (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED);
  action.value = action.pressed ? 1.0f : 0.0f;
  (void)lua_post_input(ud->input_id, &action);
}

static void btn_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* btn = lv_event_get_current_target(e);
  lvgl_btn_ud_t* ud = (lvgl_btn_ud_t*)lv_obj_get_user_data(btn);
  btn_post_input(ud, code);
}

static void apply_common_config(lua_State* L, lvgl_btn_ud_t* ud, int config_idx) {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 100;
  int32_t h = 50;
  config_idx = lua_absindex(L, config_idx);

  lua_getfield(L, config_idx, "rect");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &x);
    (void)table_get_int_index(L, -1, 2, &y);
    (void)table_get_int_index(L, -1, 3, &w);
    (void)table_get_int_index(L, -1, 4, &h);
  }
  lua_pop(L, 1);

  lua_getfield(L, config_idx, "pos");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &x);
    (void)table_get_int_index(L, -1, 2, &y);
  }
  lua_pop(L, 1);

  lua_getfield(L, config_idx, "size");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &w);
    (void)table_get_int_index(L, -1, 2, &h);
  }
  lua_pop(L, 1);

  // Explicit x/y/w/h fields intentionally override rect/pos/size.
  (void)table_get_int(L, config_idx, "x", &x);
  (void)table_get_int(L, config_idx, "y", &y);
  (void)table_get_int(L, config_idx, "w", &w);
  (void)table_get_int(L, config_idx, "h", &h);

  lv_obj_set_pos(ud->btn, x, y);
  lv_obj_set_size(ud->btn, w, h);

  lua_getfield(L, config_idx, "hidden");
  if (!lua_isnil(L, -1)) {
    if (lua_toboolean(L, -1)) lv_obj_add_flag(ud->btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ud->btn, LV_OBJ_FLAG_HIDDEN);
  }
  lua_pop(L, 1);
}

static void btn_set_text(lvgl_btn_ud_t* ud, const char* text) {
  if (!ud->label) {
    ud->label = lv_label_create(ud->btn);
    lv_obj_center(ud->label);
  }
  lv_label_set_text(ud->label, text);
  lv_obj_center(ud->label);
}

static void apply_button_style(lua_State* L, lvgl_btn_ud_t* ud, int style_idx) {
  uint32_t color = 0;
  int32_t alpha = 255;
  int32_t radius = 0;
  int32_t border_width = 0;
  style_idx = lua_absindex(L, style_idx);

  if (table_get_uint(L, style_idx, "bg", &color)) {
    lv_obj_set_style_bg_color(ud->btn, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    alpha = 255;
    (void)table_get_int(L, style_idx, "bg_alpha", &alpha);
    lv_obj_set_style_bg_opa(ud->btn, (lv_opa_t)alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else if (table_get_int(L, style_idx, "bg_alpha", &alpha)) {
    lv_obj_set_style_bg_opa(ud->btn, (lv_opa_t)alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (table_get_uint(L, style_idx, "text", &color) && ud->label) {
    lv_obj_set_style_text_color(ud->label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  lua_getfield(L, style_idx, "border");
  if (lua_istable(L, -1)) {
    if (table_get_uint(L, -1, "color", &color)) {
      lv_obj_set_style_border_color(ud->btn, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (table_get_int(L, -1, "width", &border_width)) {
      lv_obj_set_style_border_width(ud->btn, border_width, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }
  lua_pop(L, 1);

  if (table_get_int(L, style_idx, "radius", &radius)) {
    lv_obj_set_style_radius(ud->btn, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

static int apply_button_config(lua_State* L, lvgl_btn_ud_t* ud, int config_idx) {
  const char* text = NULL;
  const char* id = NULL;
  const char* input = NULL;
  config_idx = lua_absindex(L, config_idx);

  check_btn_valid(L, ud);
  apply_common_config(L, ud, config_idx);

  if (table_get_string(L, config_idx, "id", &id)) {
    snprintf(ud->id, sizeof(ud->id), "%s", id);
  }
  if (table_get_string(L, config_idx, "text", &text)) {
    btn_set_text(ud, text);
  }
  if (table_get_string(L, config_idx, "input", &input)) {
    btn_set_input_id(L, ud, input);
  }

  lua_getfield(L, config_idx, "style");
  if (lua_istable(L, -1)) {
    apply_button_style(L, ud, -1);
  }
  lua_pop(L, 1);
  return 0;
}

static int l_btn_call(lua_State* L) {
  luaL_checktype(L, 2, LUA_TTABLE);

  lvgl_btn_ud_t* ud = (lvgl_btn_ud_t*)lua_newuserdatauv(L, sizeof(lvgl_btn_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->L = L;
  snprintf(ud->input_id, sizeof(ud->input_id), "%s", "button");

  ud->btn = lv_button_create(lv_screen_active());
  if (!ud->btn) {
    lua_pushnil(L);
    lua_pushliteral(L, "failed to create button");
    return 2;
  }

  lv_obj_set_user_data(ud->btn, ud);
  ud->event_dsc = lv_obj_add_event_cb(ud->btn, btn_event_cb, LV_EVENT_ALL, NULL);
  luaL_getmetatable(L, UI_BUTTON_MT);
  lua_setmetatable(L, -2);

  apply_button_config(L, ud, 2);
  return 1;
}

static int l_btn_gc(lua_State* L) {
  lua_ui_button_delete(L, 1);
  return 0;
}

bool lua_ui_button_is(lua_State* L, int idx) {
  return test_btn(L, idx) != NULL;
}

const char* lua_ui_button_id(lua_State* L, int idx) {
  lvgl_btn_ud_t* ud = test_btn(L, idx);
  return ud ? ud->id : NULL;
}

int lua_ui_button_patch(lua_State* L, int drawable_idx, int patch_idx) {
  lvgl_btn_ud_t* ud = check_btn(L, drawable_idx);
  luaL_checktype(L, patch_idx, LUA_TTABLE);
  return apply_button_config(L, ud, patch_idx);
}

void lua_ui_button_delete(lua_State* L, int idx) {
  lvgl_btn_ud_t* ud = test_btn(L, idx);
  if (!ud) return;

  if (ud->btn) {
    if (ud->event_dsc) {
      lv_obj_remove_event_dsc(ud->btn, ud->event_dsc);
      ud->event_dsc = NULL;
    }
    lv_obj_delete(ud->btn);
    ud->btn = NULL;
    ud->label = NULL;
  }
}

static void create_button_metatable(lua_State* L) {
  if (luaL_newmetatable(L, UI_BUTTON_MT)) {
    lua_newtable(L);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_btn_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

int luaopen_ui_button(lua_State* L) {
  create_button_metatable(L);

  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, l_btn_call);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
