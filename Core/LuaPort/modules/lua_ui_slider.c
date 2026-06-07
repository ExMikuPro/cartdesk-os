// lua_ui_slider.c
// LVGL 9.5.0 滑块模块

#include "lua.h"
#include "lauxlib.h"
#include "lvgl.h"
#include <stdint.h>
#include <string.h>

#ifndef LUA_UI_SLIDER_MODNAME
#define LUA_UI_SLIDER_MODNAME "ui.slider"
#endif

// -----------------------------
// 0) 可选：取全局 cfg 的通用写法
// -----------------------------

static inline void* ui_slider_get_cfg(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "port.cfg");
  void* cfg = lua_touserdata(L, -1);
  lua_pop(L, 1);
  return cfg;
}

// -----------------------------
// 1) 参数检查/错误模板
// -----------------------------

static inline int32_t check_int32(lua_State* L, int idx) {
  return (int32_t)luaL_checkinteger(L, idx);
}

static inline uint32_t check_uint32(lua_State* L, int idx) {
  lua_Integer v = luaL_checkinteger(L, idx);
  if (v < 0) luaL_error(L, "argument %d must be non-negative", idx);
  return (uint32_t)v;
}

static inline const char* check_string(lua_State* L, int idx) {
  return luaL_checkstring(L, idx);
}

// -----------------------------
// 2) 类型定义和常量
// -----------------------------

typedef struct {
  lv_obj_t* slider;   // LVGL 滑块对象指针
  int event_cb_ref;   // Lua 事件回调函数引用（LUA_REGISTRYINDEX）
  lv_event_dsc_t* event_dsc; // LVGL 事件描述符
  lua_State* L;       // Lua 状态指针，用于事件回调
} ui_slider_ud_t;

#define UI_SLIDER_MT "ui.slider.mt"

static void lvgl_slider_event_cb(lv_event_t* e);

// -----------------------------
// 3) 辅助函数
// -----------------------------

// 检查滑块是否有效
static void check_slider_valid(ui_slider_ud_t* ud) {
  if (!ud->slider) {
    luaL_error(ud->L, "slider has been deleted");
  }
}

// 事件类型转换
static const char* event_code_to_string(lv_event_code_t code) {
  switch (code) {
    case LV_EVENT_CLICKED: return "clicked";
    case LV_EVENT_PRESSED: return "pressed";
    case LV_EVENT_RELEASED: return "released";
    case LV_EVENT_LONG_PRESSED: return "long_pressed";
    case LV_EVENT_VALUE_CHANGED: return "value_changed";
    default: return "unknown";
  }
}

// 解析对齐方式
static lv_align_t parse_align_type(const char* align_str) {
  if (strcmp(align_str, "center") == 0) return LV_ALIGN_CENTER;
  if (strcmp(align_str, "top_left") == 0) return LV_ALIGN_TOP_LEFT;
  if (strcmp(align_str, "top_mid") == 0) return LV_ALIGN_TOP_MID;
  if (strcmp(align_str, "top_right") == 0) return LV_ALIGN_TOP_RIGHT;
  if (strcmp(align_str, "bottom_left") == 0) return LV_ALIGN_BOTTOM_LEFT;
  if (strcmp(align_str, "bottom_mid") == 0) return LV_ALIGN_BOTTOM_MID;
  if (strcmp(align_str, "bottom_right") == 0) return LV_ALIGN_BOTTOM_RIGHT;
  if (strcmp(align_str, "left_mid") == 0) return LV_ALIGN_LEFT_MID;
  if (strcmp(align_str, "right_mid") == 0) return LV_ALIGN_RIGHT_MID;
  return LV_ALIGN_CENTER; // 默认居中
}

// -----------------------------
// 4) 滑块方法（slider:xxx）
// -----------------------------

// slider:set_size(width, height)
static int l_slider_set_size(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  int32_t width = check_int32(L, 2);
  int32_t height = check_int32(L, 3);

  check_slider_valid(ud);
  lv_obj_set_size(ud->slider, width, height);
  return 0;
}

// slider:set_pos(x, y)
static int l_slider_set_pos(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  int32_t x = check_int32(L, 2);
  int32_t y = check_int32(L, 3);

  check_slider_valid(ud);
  lv_obj_set_pos(ud->slider, x, y);
  return 0;
}

// slider:align(align_type, x_offset, y_offset)
static int l_slider_align(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  const char* align_str = check_string(L, 2);
  int32_t x_ofs = luaL_optinteger(L, 3, 0);
  int32_t y_ofs = luaL_optinteger(L, 4, 0);

  check_slider_valid(ud);
  lv_align_t align = parse_align_type(align_str);
  lv_obj_align(ud->slider, align, x_ofs, y_ofs);
  return 0;
}

// slider:set_value(value, anim)
static int l_slider_set_value(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  int32_t value = check_int32(L, 2);
  int anim = lua_toboolean(L, 3);

  check_slider_valid(ud);
  lv_slider_set_value(ud->slider, value, anim);
  return 0;
}

// slider:get_value() -> value
static int l_slider_get_value(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);

  check_slider_valid(ud);
  int32_t value = lv_slider_get_value(ud->slider);
  lua_pushinteger(L, value);
  return 1;
}

// slider:set_range(min, max)
static int l_slider_set_range(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  int32_t min = check_int32(L, 2);
  int32_t max = check_int32(L, 3);

  check_slider_valid(ud);
  lv_slider_set_range(ud->slider, min, max);
  return 0;
}

// slider:set_style_bg_color(color, alpha)
static int l_slider_set_style_bg_color(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t alpha = (uint8_t)luaL_optinteger(L, 3, 255);

  check_slider_valid(ud);
  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_bg_color(ud->slider, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ud->slider, alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  return 0;
}

// slider:set_style_indicator_color(color, alpha)
static int l_slider_set_style_indicator_color(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t alpha = (uint8_t)luaL_optinteger(L, 3, 255);

  check_slider_valid(ud);
  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_bg_color(ud->slider, color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ud->slider, alpha, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  return 0;
}

// slider:set_style_knob_color(color, alpha)
static int l_slider_set_style_knob_color(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t alpha = (uint8_t)luaL_optinteger(L, 3, 255);

  check_slider_valid(ud);
  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_bg_color(ud->slider, color, LV_PART_KNOB | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ud->slider, alpha, LV_PART_KNOB | LV_STATE_DEFAULT);
  return 0;
}

// slider:set_style_border(color, width)
static int l_slider_set_style_border(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t width = (uint8_t)luaL_optinteger(L, 3, 1);

  check_slider_valid(ud);
  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_border_color(ud->slider, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ud->slider, width, LV_PART_MAIN | LV_STATE_DEFAULT);
  return 0;
}

// slider:set_style_radius(radius)
static int l_slider_set_style_radius(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);
  uint8_t radius = (uint8_t)luaL_checkinteger(L, 2);

  check_slider_valid(ud);
  lv_obj_set_style_radius(ud->slider, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(ud->slider, radius, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(ud->slider, radius, LV_PART_KNOB | LV_STATE_DEFAULT);
  return 0;
}

// slider:set_callback(callback)
static int l_slider_set_callback(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);

  check_slider_valid(ud);
  luaL_argcheck(L, lua_isfunction(L, 2) || lua_isnil(L, 2), 2, "expected function or nil");
  
  // 清除之前的回调
  if (ud->event_cb_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, ud->event_cb_ref);
    ud->event_cb_ref = LUA_NOREF;
  }

  // 设置新回调
  if (lua_isfunction(L, 2)) {
    lua_pushvalue(L, 2);
    ud->event_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ud->L = L;
    if (ud->event_dsc == NULL) {
      ud->event_dsc = lv_obj_add_event_cb(ud->slider, lvgl_slider_event_cb, LV_EVENT_ALL, NULL);
    }
  } else if (ud->event_dsc != NULL) {
    lv_obj_remove_event_dsc(ud->slider, ud->event_dsc);
    ud->event_dsc = NULL;
  }

  return 0;
}

// slider:delete()
static int l_slider_delete(lua_State* L) {
  ui_slider_ud_t* ud = (ui_slider_ud_t*)luaL_checkudata(L, 1, UI_SLIDER_MT);

  if (ud->slider) {
    if (ud->event_dsc != NULL) {
      lv_obj_remove_event_dsc(ud->slider, ud->event_dsc);
      ud->event_dsc = NULL;
    }
    lv_obj_delete(ud->slider);
    ud->slider = NULL;
  }

  if (ud->event_cb_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, ud->event_cb_ref);
    ud->event_cb_ref = LUA_NOREF;
  }

  return 0;
}

static const luaL_Reg ui_slider_methods[] = {
  {"set_size", l_slider_set_size},
  {"set_pos", l_slider_set_pos},
  {"align", l_slider_align},
  {"set_value", l_slider_set_value},
  {"get_value", l_slider_get_value},
  {"set_range", l_slider_set_range},
  {"set_style_bg_color", l_slider_set_style_bg_color},
  {"set_style_indicator_color", l_slider_set_style_indicator_color},
  {"set_style_knob_color", l_slider_set_style_knob_color},
  {"set_style_border", l_slider_set_style_border},
  {"set_style_radius", l_slider_set_style_radius},
  {"set_callback", l_slider_set_callback},
  {"delete", l_slider_delete},
  {NULL, NULL}
};

// -----------------------------
// 5) 滑块事件回调
// -----------------------------

static void lvgl_slider_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* slider = lv_event_get_current_target(e);

  // 从 user_data 获取用户数据
  ui_slider_ud_t* ud = (ui_slider_ud_t*)lv_obj_get_user_data(slider);
  if (!ud || ud->event_cb_ref == LUA_NOREF || !ud->L) return;

  // 调用 Lua 回调函数
  lua_State* L = ud->L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ud->event_cb_ref);
  
  // 推送滑块对象
  lua_pushlightuserdata(L, ud);
  
  // 推送事件类型
  lua_pushstring(L, event_code_to_string(code));
  
  // 调用回调函数
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    const char* err = lua_tostring(L, -1);
    if (err) {
      // 这里可以添加日志
    }
    lua_pop(L, 1);
  }
}

// -----------------------------
// 6) 模块表 API
// -----------------------------

// ui.slider.create(parent) -> slider_userdata
static int l_slider_create(lua_State* L) {
  lv_obj_t* parent = NULL;

  // 检查第一个参数是否为 userdata（父对象）
  if (lua_isuserdata(L, 1)) {
    parent = (lv_obj_t*)lua_touserdata(L, 1);
  } else if (lua_isnil(L, 1) || lua_gettop(L) == 0) {
    // 使用当前活动屏幕
    parent = lv_screen_active();
  } else {
    return luaL_error(L, "invalid parent object");
  }

  // 创建 userdata
  ui_slider_ud_t* ud = (ui_slider_ud_t*)lua_newuserdatauv(L, sizeof(ui_slider_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->event_cb_ref = LUA_NOREF;
  ud->L = L;

  // 创建 LVGL 滑块
  ud->slider = lv_slider_create(parent);

  if (!ud->slider) {
    return luaL_error(L, "failed to create slider");
  }

  // 保存 userdata 指针到 LVGL 对象
  lv_obj_set_user_data(ud->slider, ud);

  // 绑定 metatable
  luaL_getmetatable(L, UI_SLIDER_MT);
  lua_setmetatable(L, -2);

  return 1;
}

// ui.slider.draw(parent, x, y, width, height) -> slider_userdata
static int l_slider_draw(lua_State* L) {
  lv_obj_t* parent = NULL;

  // 检查第一个参数是否为 userdata（父对象）
  if (lua_isuserdata(L, 1)) {
    parent = (lv_obj_t*)lua_touserdata(L, 1);
  } else if (lua_isnil(L, 1) || lua_gettop(L) == 0) {
    // 使用当前活动屏幕
    parent = lv_screen_active();
  } else {
    return luaL_error(L, "invalid parent object");
  }

  // 获取位置和大小参数
  int32_t x = luaL_optinteger(L, 2, 0);
  int32_t y = luaL_optinteger(L, 3, 0);
  int32_t width = luaL_optinteger(L, 4, 200);
  int32_t height = luaL_optinteger(L, 5, 20);

  // 创建 userdata
  ui_slider_ud_t* ud = (ui_slider_ud_t*)lua_newuserdatauv(L, sizeof(ui_slider_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->event_cb_ref = LUA_NOREF;
  ud->L = L;

  // 创建 LVGL 滑块
  ud->slider = lv_slider_create(parent);

  if (!ud->slider) {
    return luaL_error(L, "failed to create slider");
  }

  // 设置位置和大小
  lv_obj_set_pos(ud->slider, x, y);
  lv_obj_set_size(ud->slider, width, height);

  // 保存 userdata 指针到 LVGL 对象
  lv_obj_set_user_data(ud->slider, ud);

  // 绑定 metatable
  luaL_getmetatable(L, UI_SLIDER_MT);
  lua_setmetatable(L, -2);

  return 1;
}

// ui.slider.get_screen() -> screen_object
static int l_slider_get_screen(lua_State* L) {
  lv_obj_t* scr = lv_screen_active();
  lua_pushlightuserdata(L, scr);
  return 1;
}

static const luaL_Reg ui_slider_funcs[] = {
  {"create", l_slider_create},
  {"draw", l_slider_draw},
  {"get_screen", l_slider_get_screen},
  {NULL, NULL}
};

// -----------------------------
// 7) 创建 metatable
// -----------------------------

static void ui_slider_create_metatable(lua_State* L) {
  if (luaL_newmetatable(L, UI_SLIDER_MT)) {
    // mt.__index = methods_table
    lua_newtable(L);
    luaL_setfuncs(L, ui_slider_methods, 0);
    lua_setfield(L, -2, "__index");

    // mt.__gc = delete
    lua_pushcfunction(L, l_slider_delete);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1); // pop metatable
}

// -----------------------------
// 8) 模块导出
// -----------------------------

int luaopen_ui_slider(lua_State* L) {
  // 创建 metatable
  ui_slider_create_metatable(L);

  // 创建模块表
  luaL_newlib(L, ui_slider_funcs);

  return 1; // 返回模块表
}
