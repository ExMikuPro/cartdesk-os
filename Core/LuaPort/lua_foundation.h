#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lua.h"
#include "task_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_FOUNDATION_APP_ID_MAX 64u

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  uint64_t cart_id;
  const char* app_id;
} lua_foundation_owner_view_t;

void lua_foundation_registry_init(void);
lua_State* lua_foundation_main_thread(lua_State* L);
bool lua_foundation_same_vm(lua_State* left, lua_State* right);
bool lua_foundation_owner_create(lua_State* L,
                                 uint32_t owner_id,
                                 uint32_t generation,
                                 uint64_t cart_id,
                                 const char* app_id);
void lua_foundation_owner_destroy(lua_State* L,
                                  uint32_t owner_id,
                                  uint32_t generation);
void lua_foundation_owner_enter(lua_State* L,
                                uint32_t owner_id,
                                uint32_t generation);
void lua_foundation_owner_leave(void);
bool lua_foundation_current(lua_State* L,
                            lua_foundation_owner_view_t* out_owner);
bool lua_foundation_log_allow(uint64_t now_ms, uint32_t* out_dropped);
void lua_foundation_process(lua_State* L, uint64_t now_ms);
bool lua_foundation_handle_io_completion(const cart_io_completion_t* completion);
bool lua_foundation_storage_ready(void);

#ifdef __cplusplus
}
#endif
