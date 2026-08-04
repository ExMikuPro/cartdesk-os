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
#define LUA_RUNTIME_ERROR_MESSAGE_MAX 192u
#define LUA_RUNTIME_ERROR_TRACEBACK_MAX 512u
#define LUA_RUNTIME_ERROR_APP_ID_MAX 64u

typedef enum {
  LUA_RUNTIME_ERROR_STAGE_NONE = 0,
  LUA_RUNTIME_ERROR_STAGE_LOAD,
  LUA_RUNTIME_ERROR_STAGE_INIT,
  LUA_RUNTIME_ERROR_STAGE_UPDATE,
  LUA_RUNTIME_ERROR_STAGE_INPUT,
  LUA_RUNTIME_ERROR_STAGE_TIMER,
  LUA_RUNTIME_ERROR_STAGE_MESSAGE,
  LUA_RUNTIME_ERROR_STAGE_FINAL,
} LuaRuntimeErrorStage;

typedef struct {
  LuaRuntimeErrorStage stage;
  char message[LUA_RUNTIME_ERROR_MESSAGE_MAX];
  char traceback[LUA_RUNTIME_ERROR_TRACEBACK_MAX];
  char app_id[LUA_RUNTIME_ERROR_APP_ID_MAX + 1u];
  uint32_t owner_id;
  uint64_t cart_id;
  uint32_t tick;
} LuaRuntimeErrorInfo;

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
#if PERF_MONITOR_ENABLE
int  lua_init_from_source_for_stability_test(const char *source,
                                             const char *chunk_name);
#endif
int  lua_run_bytecode(const void *bytecode, uint32_t len, const char *chunk_name);
int  lua_run_cart_entry(const char *cart_path);
int  lua_run_file(const char *path);
int  lua_reload(void);
int  lua_shutdown(void);
int  lua_post_input(const char *action_id, const LuaInputAction *action);
int  lua_post_input_for_owner(uint32_t owner_id,
                              uint32_t generation,
                              const char *action_id,
                              const LuaInputAction *action);
int  lua_post_message(const char *message_id, const char *sender);
void lua_update_task(void);
void lua_rt_delay_ms(uint32_t delay_ms);
const char *lua_current_cart_path(void);
uint32_t lua_vm_input_queue_len(void);
uint32_t lua_vm_input_queue_capacity(void);
uint32_t lua_vm_message_queue_len(void);
uint32_t lua_vm_message_queue_capacity(void);
uint32_t lua_vm_runtime_state(void);
bool lua_vm_get_runtime_error(LuaRuntimeErrorInfo *out_error);
const char *lua_vm_runtime_error_stage_name(LuaRuntimeErrorStage stage);
void lua_vm_report_timer_error(uint32_t owner_id,
                               uint32_t generation,
                               const char *app_id,
                               const char *message);

#ifdef __cplusplus
}
#endif
