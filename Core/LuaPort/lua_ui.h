#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lua.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_UI_DEBUG_ID_MAX 32u
#define LUA_UI_ERROR_MAX 192u

#define LUA_UI_HANDLE_MT "cartdesk.ui_handle"

typedef enum {
  LUA_UI_OBJECT_LABEL = 1,
  LUA_UI_OBJECT_BUTTON,
  LUA_UI_OBJECT_IMAGE,
} lua_ui_object_type_t;

typedef struct lua_ui_handle lua_ui_handle_t;
typedef void (*lua_ui_handle_cleanup_cb_t)(lua_ui_handle_t* handle);

/**
 * Lua UI安全句柄的公共头。各控件userdata必须把它作为第一个字段。
 * Lua只能持有full userdata，不能取得object中的LVGL裸指针。
 */
struct lua_ui_handle {
  lv_obj_t* object;
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  uint16_t object_type;
  bool alive;
  bool registered;
  int lua_ref;
  lua_ui_handle_cleanup_cb_t cleanup;
  lua_ui_handle_t* owner_next;
  char debug_id[LUA_UI_DEBUG_ID_MAX];
};

int luaopen_ui_label(lua_State* L);
int luaopen_ui_button(lua_State* L);
int luaopen_ui_image(lua_State* L);

void lua_ui_registry_init(void);
bool lua_ui_owner_create(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_ui_owner_destroy(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_ui_owner_enter(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_ui_owner_leave(void);
lv_obj_t* lua_ui_owner_root(lua_State* L);

lua_ui_handle_t* lua_ui_handle_new(lua_State* L,
                                   size_t userdata_size,
                                   lua_ui_object_type_t object_type);
bool lua_ui_handle_register(lua_State* L,
                            int userdata_idx,
                            lua_ui_handle_t* handle,
                            lv_obj_t* object,
                            lua_ui_handle_cleanup_cb_t cleanup);
void lua_ui_handle_delete(lua_ui_handle_t* handle);
lua_ui_handle_t* lua_ui_handle_test(lua_State* L, int idx);
lua_ui_handle_t* lua_ui_handle_validate(lua_State* L,
                                        int idx,
                                        char* error,
                                        size_t error_size);
const char* lua_ui_object_type_name(uint16_t object_type);

bool lua_ui_validate_properties(lua_State* L,
                                int table_idx,
                                const char* const* allowed,
                                size_t allowed_count,
                                const lua_ui_handle_t* handle,
                                char* error,
                                size_t error_size);
bool lua_ui_apply_rect(lua_State* L,
                       int table_idx,
                       lv_obj_t* object,
                       int32_t default_x,
                       int32_t default_y,
                       int32_t default_w,
                       int32_t default_h,
                       char* error,
                       size_t error_size);
bool lua_ui_apply_hidden(lua_State* L,
                         int table_idx,
                         lv_obj_t* object,
                         char* error,
                         size_t error_size);
bool lua_ui_read_optional_string(lua_State* L,
                                 int table_idx,
                                 const char* key,
                                 const char** value,
                                 bool* present,
                                 char* error,
                                 size_t error_size);
int lua_ui_push_error(lua_State* L, const char* error);
int lua_ui_push_object_error(lua_State* L,
                             const lua_ui_handle_t* handle,
                             const char* property,
                             const char* detail);
int lua_ui_patch(lua_State* L);
bool lua_ui_label_patch(lua_State* L,
                        lua_ui_handle_t* handle,
                        int properties_idx,
                        char* error,
                        size_t error_size);
bool lua_ui_button_patch(lua_State* L,
                         lua_ui_handle_t* handle,
                         int properties_idx,
                         char* error,
                         size_t error_size);
bool lua_ui_image_patch(lua_State* L,
                        lua_ui_handle_t* handle,
                        int properties_idx,
                        char* error,
                        size_t error_size);

#ifdef __cplusplus
}
#endif
