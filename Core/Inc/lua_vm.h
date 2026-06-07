#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_INPUT_ACTION_ID_MAX  24u
#define LUA_INPUT_EVENT_MAX      24u
#define LUA_MESSAGE_ID_MAX       32u
#define LUA_MESSAGE_SENDER_MAX   32u

typedef struct {
  char event[LUA_INPUT_EVENT_MAX];
  bool pressed;
  bool released;
  bool repeated;
  float value;
  float x;
  float y;
  float dx;
  float dy;
} LuaInputAction;

int  lua_init(void);
int  lua_init_from_cart(const char *cart_path);
int  lua_init_from_file(const char *path);
int  lua_run_bytecode(const void *bytecode, uint32_t len, const char *chunk_name);
int  lua_run_cart_entry(const char *cart_path);
int  lua_run_file(const char *path);
int  lua_reload(void);
int  lua_shutdown(void);
int  lua_post_input(const char *action_id, const LuaInputAction *action);
int  lua_post_message(const char *message_id, const char *sender);
void lua_update_task(void);
void lua_rt_delay_ms(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
