#include "lua_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua_vm.h"
#include "lvgl.h"

typedef struct {
  lv_obj_t* slider;
  lv_event_dsc_t* event_dsc;
  lua_State* L;
  char id[LUA_UI_DRAWABLE_ID_MAX];
  char input_id[LUA_INPUT_ACTION_ID_MAX];
} ui_slider_ud_t;

#define UI_SLIDER_MT "ui.slider.mt"

static ui_slider_ud_t* test_slider(lua_State* L, int idx) {
  return (ui_slider_ud_t*)luaL_testudata(L, idx, UI_SLIDER_MT);
}

static ui_slider_ud_t* check_slider(lua_State* L, int idx) {
  return (ui_slider_ud_t*)luaL_checkudata(L, idx, UI_SLIDER_MT);
}

static void check_slider_valid(lua_State* L, ui_slider_ud_t* ud) {
  if (!ud || !ud->slider) {
    luaL_error(L, "slider has been deleted");
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

static void slider_set_input_id(lua_State* L, ui_slider_ud_t* ud, const char* input_id) {
  luaL_argcheck(L, input_id && input_id[0] != '\0', 1, "input must not be empty");
  luaL_argcheck(L, strlen(input_id) < LUA_INPUT_ACTION_ID_MAX, 1, "input is too long");
  snprintf(ud->input_id, sizeof(ud->input_id), "%s", input_id);
}

static void slider_post_input(ui_slider_ud_t* ud, lv_event_code_t code) {
  if (!ud || !ud->slider || code != LV_EVENT_VALUE_CHANGED) return;

  LuaInputAction action = {0};
  snprintf(action.event, sizeof(action.event), "%s", "changed");
  action.value = (float)lv_slider_get_value(ud->slider);
  (void)lua_post_input(ud->input_id, &action);
}

static void slider_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* slider = lv_event_get_current_target(e);
  ui_slider_ud_t* ud = (ui_slider_ud_t*)lv_obj_get_user_data(slider);
  slider_post_input(ud, code);
}

static void apply_common_config(lua_State* L, ui_slider_ud_t* ud, int config_idx) {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 200;
  int32_t h = 20;
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

  lv_obj_set_pos(ud->slider, x, y);
  lv_obj_set_size(ud->slider, w, h);

  lua_getfield(L, config_idx, "hidden");
  if (!lua_isnil(L, -1)) {
    if (lua_toboolean(L, -1)) lv_obj_add_flag(ud->slider, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ud->slider, LV_OBJ_FLAG_HIDDEN);
  }
  lua_pop(L, 1);
}

static void apply_slider_style(lua_State* L, ui_slider_ud_t* ud, int style_idx) {
  uint32_t color = 0;
  int32_t alpha = 255;
  int32_t radius = 0;
  int32_t border_width = 0;
  style_idx = lua_absindex(L, style_idx);

  if (table_get_uint(L, style_idx, "bg", &color)) {
    lv_obj_set_style_bg_color(ud->slider, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    alpha = 255;
    (void)table_get_int(L, style_idx, "bg_alpha", &alpha);
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else if (table_get_int(L, style_idx, "bg_alpha", &alpha)) {
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (table_get_uint(L, style_idx, "indicator", &color)) {
    lv_obj_set_style_bg_color(ud->slider, lv_color_hex(color), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    alpha = 255;
    (void)table_get_int(L, style_idx, "indicator_alpha", &alpha);
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  } else if (table_get_int(L, style_idx, "indicator_alpha", &alpha)) {
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  }

  if (table_get_uint(L, style_idx, "knob", &color)) {
    lv_obj_set_style_bg_color(ud->slider, lv_color_hex(color), LV_PART_KNOB | LV_STATE_DEFAULT);
    alpha = 255;
    (void)table_get_int(L, style_idx, "knob_alpha", &alpha);
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_KNOB | LV_STATE_DEFAULT);
  } else if (table_get_int(L, style_idx, "knob_alpha", &alpha)) {
    lv_obj_set_style_bg_opa(ud->slider, (lv_opa_t)alpha, LV_PART_KNOB | LV_STATE_DEFAULT);
  }

  lua_getfield(L, style_idx, "border");
  if (lua_istable(L, -1)) {
    if (table_get_uint(L, -1, "color", &color)) {
      lv_obj_set_style_border_color(ud->slider, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (table_get_int(L, -1, "width", &border_width)) {
      lv_obj_set_style_border_width(ud->slider, border_width, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }
  lua_pop(L, 1);

  if (table_get_int(L, style_idx, "radius", &radius)) {
    lv_obj_set_style_radius(ud->slider, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ud->slider, radius, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ud->slider, radius, LV_PART_KNOB | LV_STATE_DEFAULT);
  }
}

static int apply_slider_config(lua_State* L, ui_slider_ud_t* ud, int config_idx) {
  const char* id = NULL;
  const char* input = NULL;
  int32_t min = 0;
  int32_t max = 100;
  int32_t value = 0;
  config_idx = lua_absindex(L, config_idx);

  check_slider_valid(L, ud);
  apply_common_config(L, ud, config_idx);

  if (table_get_string(L, config_idx, "id", &id)) {
    snprintf(ud->id, sizeof(ud->id), "%s", id);
  }
  if (table_get_string(L, config_idx, "input", &input)) {
    slider_set_input_id(L, ud, input);
  }

  lua_getfield(L, config_idx, "range");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &min);
    (void)table_get_int_index(L, -1, 2, &max);
    lv_slider_set_range(ud->slider, min, max);
  }
  lua_pop(L, 1);

  if (table_get_int(L, config_idx, "value", &value)) {
    lv_slider_set_value(ud->slider, value, LV_ANIM_OFF);
  }

  lua_getfield(L, config_idx, "style");
  if (lua_istable(L, -1)) {
    apply_slider_style(L, ud, -1);
  }
  lua_pop(L, 1);
  return 0;
}

static int l_slider_call(lua_State* L) {
  luaL_checktype(L, 2, LUA_TTABLE);

  ui_slider_ud_t* ud = (ui_slider_ud_t*)lua_newuserdatauv(L, sizeof(ui_slider_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->L = L;
  snprintf(ud->input_id, sizeof(ud->input_id), "%s", "slider");

  ud->slider = lv_slider_create(lv_screen_active());
  if (!ud->slider) {
    lua_pushnil(L);
    lua_pushliteral(L, "failed to create slider");
    return 2;
  }

  lv_obj_set_user_data(ud->slider, ud);
  ud->event_dsc = lv_obj_add_event_cb(ud->slider, slider_event_cb, LV_EVENT_ALL, NULL);
  luaL_getmetatable(L, UI_SLIDER_MT);
  lua_setmetatable(L, -2);

  apply_slider_config(L, ud, 2);
  return 1;
}

static int l_slider_gc(lua_State* L) {
  lua_ui_slider_delete(L, 1);
  return 0;
}

bool lua_ui_slider_is(lua_State* L, int idx) {
  return test_slider(L, idx) != NULL;
}

const char* lua_ui_slider_id(lua_State* L, int idx) {
  ui_slider_ud_t* ud = test_slider(L, idx);
  return ud ? ud->id : NULL;
}

int lua_ui_slider_patch(lua_State* L, int drawable_idx, int patch_idx) {
  ui_slider_ud_t* ud = check_slider(L, drawable_idx);
  luaL_checktype(L, patch_idx, LUA_TTABLE);
  return apply_slider_config(L, ud, patch_idx);
}

void lua_ui_slider_delete(lua_State* L, int idx) {
  ui_slider_ud_t* ud = test_slider(L, idx);
  if (!ud) return;

  if (ud->slider) {
    if (ud->event_dsc) {
      lv_obj_remove_event_dsc(ud->slider, ud->event_dsc);
      ud->event_dsc = NULL;
    }
    lv_obj_delete(ud->slider);
    ud->slider = NULL;
  }
}

static void create_slider_metatable(lua_State* L) {
  if (luaL_newmetatable(L, UI_SLIDER_MT)) {
    lua_newtable(L);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_slider_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

int luaopen_ui_slider(lua_State* L) {
  create_slider_metatable(L);

  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, l_slider_call);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
