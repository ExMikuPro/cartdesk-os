// lua_lvgl_btn.c
// LVGL 9.5.0 按钮模块 - 重构版本

#include "lua.h"
#include "lauxlib.h"
#include "lvgl.h"
#include <stdint.h>
#include <string.h>

#ifndef LUA_LVGL_BTN_MODNAME
#define LUA_LVGL_BTN_MODNAME "lvgl_btn"
#endif

// -----------------------------
// 类型定义和常量
// -----------------------------

typedef struct {
  lv_obj_t* btn;      // LVGL 按钮对象指针
  lv_obj_t* label;    // 按钮上的标签（可选）
  int event_cb_ref;   // Lua 事件回调函数引用（LUA_REGISTRYINDEX）
  lv_event_dsc_t* event_dsc; // LVGL 事件描述符
  lua_State* L;       // Lua 状态指针，用于事件回调
} lvgl_btn_ud_t;

#define UI_BUTTON_MT "ui.button.mt"

// -----------------------------
// 辅助函数
// -----------------------------

// 获取全局配置
static inline void* lvgl_btn_get_cfg(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "port.cfg");
  void* cfg = lua_touserdata(L, -1);
  lua_pop(L, 1);
  return cfg;
}

// 参数检查
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

// 类型检查
static lvgl_btn_ud_t* check_btn_userdata(lua_State* L, int idx) {
  return (lvgl_btn_ud_t*)luaL_checkudata(L, idx, UI_BUTTON_MT);
}

// 检查按钮是否有效
static void check_btn_valid(lvgl_btn_ud_t* ud) {
  if (!ud->btn) {
    luaL_error(ud->L, "button has been deleted");
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

// -----------------------------
// LVGL 事件回调
// -----------------------------

static void lvgl_btn_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* btn = lv_event_get_current_target(e);

  // 从 user_data 获取用户数据
  lvgl_btn_ud_t* ud = (lvgl_btn_ud_t*)lv_obj_get_user_data(btn);
  if (!ud || ud->event_cb_ref == LUA_NOREF || !ud->L) return;

  // 调用 Lua 回调函数
  lua_State* L = ud->L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ud->event_cb_ref);

  // 推送按钮对象
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
// 按钮方法（btn:xxx）
// -----------------------------

// 辅助函数：解析对齐方式
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

// 辅助函数：解析标志
static lv_obj_flag_t parse_flag(const char* flag_str) {
  if (strcmp(flag_str, "hidden") == 0) return LV_OBJ_FLAG_HIDDEN;
  if (strcmp(flag_str, "clickable") == 0) return LV_OBJ_FLAG_CLICKABLE;
  if (strcmp(flag_str, "checkable") == 0) return LV_OBJ_FLAG_CHECKABLE;
  if (strcmp(flag_str, "scrollable") == 0) return LV_OBJ_FLAG_SCROLLABLE;
  if (strcmp(flag_str, "press_lock") == 0) return LV_OBJ_FLAG_PRESS_LOCK;
  return 0;
}

// btn:set_text(text)
static int l_btn_set_text(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  const char* text = check_string(L, 2);

  check_btn_valid(ud);

  if (ud->label) {
    lv_label_set_text(ud->label, text);
  } else {
    // 创建标签
    ud->label = lv_label_create(ud->btn);
    lv_label_set_text(ud->label, text);
    lv_obj_center(ud->label);
  }

  return 0;
}

// btn:set_size(width, height)
static int l_btn_set_size(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  int32_t width = check_int32(L, 2);
  int32_t height = check_int32(L, 3);

  check_btn_valid(ud);

  lv_obj_set_size(ud->btn, width, height);
  return 0;
}

// btn:set_pos(x, y)
static int l_btn_set_pos(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  int32_t x = check_int32(L, 2);
  int32_t y = check_int32(L, 3);

  check_btn_valid(ud);

  lv_obj_set_pos(ud->btn, x, y);
  return 0;
}

// btn:align(align_type, x_offset, y_offset)
static int l_btn_align(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  const char* align_str = check_string(L, 2);
  int32_t x_ofs = luaL_optinteger(L, 3, 0);
  int32_t y_ofs = luaL_optinteger(L, 4, 0);

  check_btn_valid(ud);

  lv_align_t align = parse_align_type(align_str);
  lv_obj_align(ud->btn, align, x_ofs, y_ofs);
  return 0;
}

// btn:set_style_bg_color(color, alpha)
static int l_btn_set_style_bg_color(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t alpha = (uint8_t)luaL_optinteger(L, 3, 255);

  check_btn_valid(ud);

  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_bg_color(ud->btn, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ud->btn, alpha, LV_PART_MAIN | LV_STATE_DEFAULT);

  return 0;
}

// btn:set_style_text_color(color)
static int l_btn_set_style_text_color(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  uint32_t color_hex = check_uint32(L, 2);

  check_btn_valid(ud);

  if (ud->label) {
    lv_color_t color = lv_color_hex(color_hex);
    lv_obj_set_style_text_color(ud->label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  return 0;
}

// btn:set_style_border(color, width)
static int l_btn_set_style_border(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  uint32_t color_hex = check_uint32(L, 2);
  uint8_t width = (uint8_t)luaL_optinteger(L, 3, 1);

  check_btn_valid(ud);

  lv_color_t color = lv_color_hex(color_hex);
  lv_obj_set_style_border_color(ud->btn, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ud->btn, width, LV_PART_MAIN | LV_STATE_DEFAULT);

  return 0;
}

// btn:set_style_radius(radius)
static int l_btn_set_style_radius(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  uint8_t radius = (uint8_t)luaL_checkinteger(L, 2);

  check_btn_valid(ud);

  lv_obj_set_style_radius(ud->btn, radius, LV_PART_MAIN | LV_STATE_DEFAULT);

  return 0;
}

// btn:add_flag(flag_name)
static int l_btn_add_flag(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  const char* flag_str = check_string(L, 2);

  check_btn_valid(ud);

  lv_obj_flag_t flag = parse_flag(flag_str);
  if (flag != 0) {
    lv_obj_add_flag(ud->btn, flag);
  }

  return 0;
}

// btn:clear_flag(flag_name)
static int l_btn_clear_flag(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  const char* flag_str = check_string(L, 2);

  check_btn_valid(ud);

  lv_obj_flag_t flag = parse_flag(flag_str);
  if (flag != 0) {
    lv_obj_remove_flag(ud->btn, flag);
  }

  return 0;
}

// btn:set_checkable(enable)
static int l_btn_set_checkable(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);
  int enable = lua_toboolean(L, 2);

  check_btn_valid(ud);

  if (enable) {
    lv_obj_add_flag(ud->btn, LV_OBJ_FLAG_CHECKABLE);
  } else {
    lv_obj_remove_flag(ud->btn, LV_OBJ_FLAG_CHECKABLE);
  }

  return 0;
}

// btn:is_checked() -> boolean
static int l_btn_is_checked(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);

  check_btn_valid(ud);

  int checked = lv_obj_has_state(ud->btn, LV_STATE_CHECKED);
  lua_pushboolean(L, checked);
  return 1;
}

// btn:set_callback(callback)
static int l_btn_set_callback(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);

  check_btn_valid(ud);
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
      ud->event_dsc = lv_obj_add_event_cb(ud->btn, lvgl_btn_event_cb, LV_EVENT_ALL, NULL);
    }
  } else if (ud->event_dsc != NULL) {
    lv_obj_remove_event_dsc(ud->btn, ud->event_dsc);
    ud->event_dsc = NULL;
  }

  return 0;
}

// btn:delete()
static int l_btn_delete(lua_State* L) {
  lvgl_btn_ud_t* ud = check_btn_userdata(L, 1);

  if (ud->btn) {
    if (ud->event_dsc != NULL) {
      lv_obj_remove_event_dsc(ud->btn, ud->event_dsc);
      ud->event_dsc = NULL;
    }
    lv_obj_delete(ud->btn);
    ud->btn = NULL;
    ud->label = NULL;
  }

  if (ud->event_cb_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, ud->event_cb_ref);
    ud->event_cb_ref = LUA_NOREF;
  }

  return 0;
}

static const luaL_Reg lvgl_btn_methods[] = {
  {"set_text", l_btn_set_text},
  {"set_size", l_btn_set_size},
  {"set_pos", l_btn_set_pos},
  {"align", l_btn_align},
  {"set_style_bg_color", l_btn_set_style_bg_color},
  {"set_style_text_color", l_btn_set_style_text_color},
  {"set_style_border", l_btn_set_style_border},
  {"set_style_radius", l_btn_set_style_radius},
  {"add_flag", l_btn_add_flag},
  {"clear_flag", l_btn_clear_flag},
  {"set_checkable", l_btn_set_checkable},
  {"is_checked", l_btn_is_checked},
  {"set_callback", l_btn_set_callback},
  {"delete", l_btn_delete},
  {NULL, NULL}
};

// -----------------------------
// 模块表 API
// -----------------------------

// lvgl_btn.create(parent) -> btn_userdata
static int l_lvgl_btn_create(lua_State* L) {
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
  lvgl_btn_ud_t* ud = (lvgl_btn_ud_t*)lua_newuserdatauv(L, sizeof(lvgl_btn_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->event_cb_ref = LUA_NOREF;
  ud->L = L;

  // 创建 LVGL 按钮
  ud->btn = lv_button_create(parent);
  ud->label = NULL;

  if (!ud->btn) {
    return luaL_error(L, "failed to create button");
  }

  // 保存 userdata 指针到 LVGL 对象
  lv_obj_set_user_data(ud->btn, ud);

  // 绑定 metatable
  luaL_getmetatable(L, UI_BUTTON_MT);
  lua_setmetatable(L, -2);

  return 1;
}

// lvgl_btn.draw(parent, x, y, width, height, text) -> btn_userdata
static int l_lvgl_btn_draw(lua_State* L) {
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
  int32_t width = luaL_optinteger(L, 4, 100);
  int32_t height = luaL_optinteger(L, 5, 50);
  const char* text = luaL_optstring(L, 6, "Button");

  // 创建 userdata
  lvgl_btn_ud_t* ud = (lvgl_btn_ud_t*)lua_newuserdatauv(L, sizeof(lvgl_btn_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->event_cb_ref = LUA_NOREF;
  ud->L = L;

  // 创建 LVGL 按钮
  ud->btn = lv_button_create(parent);

  if (!ud->btn) {
    return luaL_error(L, "failed to create button");
  }

  // 设置位置和大小
  lv_obj_set_pos(ud->btn, x, y);
  lv_obj_set_size(ud->btn, width, height);

  // 设置文本
  ud->label = lv_label_create(ud->btn);
  lv_label_set_text(ud->label, text);
  lv_obj_center(ud->label);

  // 保存 userdata 指针到 LVGL 对象
  lv_obj_set_user_data(ud->btn, ud);

  // 绑定 metatable
  luaL_getmetatable(L, UI_BUTTON_MT);
  lua_setmetatable(L, -2);

  return 1;
}

// lvgl_btn.get_screen() -> screen_object
static int l_lvgl_btn_get_screen(lua_State* L) {
  lv_obj_t* scr = lv_screen_active();
  lua_pushlightuserdata(L, scr);
  return 1;
}

static const luaL_Reg lvgl_btn_funcs[] = {
  {"create", l_lvgl_btn_create},
  {"draw", l_lvgl_btn_draw},
  {"get_screen", l_lvgl_btn_get_screen},
  {NULL, NULL}
};

// -----------------------------
// 创建 metatable
// -----------------------------

static void ui_button_create_metatable(lua_State* L) {
  if (luaL_newmetatable(L, UI_BUTTON_MT)) {
    // mt.__index = methods_table
    lua_newtable(L);
    luaL_setfuncs(L, lvgl_btn_methods, 0);
    lua_setfield(L, -2, "__index");

    // mt.__gc = delete
    lua_pushcfunction(L, l_btn_delete);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1); // pop metatable
}

// -----------------------------
// 模块导出
// -----------------------------

int luaopen_ui_button(lua_State* L) {
  // 创建 metatable
  ui_button_create_metatable(L);

  // 创建模块表
  luaL_newlib(L, lvgl_btn_funcs);

  return 1; // 返回模块表
}
