#pragma once

#include <stdbool.h>

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_UI_DRAWABLE_ID_MAX 32u

int luaopen_ui_button(lua_State* L);
int luaopen_ui_slider(lua_State* L);

bool lua_ui_button_is(lua_State* L, int idx);
bool lua_ui_slider_is(lua_State* L, int idx);

const char* lua_ui_button_id(lua_State* L, int idx);
const char* lua_ui_slider_id(lua_State* L, int idx);

int lua_ui_button_patch(lua_State* L, int drawable_idx, int patch_idx);
int lua_ui_slider_patch(lua_State* L, int drawable_idx, int patch_idx);

void lua_ui_button_delete(lua_State* L, int idx);
void lua_ui_slider_delete(lua_State* L, int idx);
void lua_ui_delete_children(lua_State* L, int idx);

#ifdef __cplusplus
}
#endif
