#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lua.h"
#include "resource_manager.h"

#define LUA_ASSET_HANDLE_MT "cartdesk.asset_handle"

int luaopen_assets(lua_State* L);
bool lua_assets_owner_create(lua_State* L, uint32_t owner_id, uint32_t generation);
void lua_assets_owner_destroy(lua_State* L, uint32_t owner_id, uint32_t generation);
bool lua_asset_image_acquire(lua_State* L, int index, res_handle_t* out_handle,
                             const image_resource_t** out_image,
                             const char** out_error);
