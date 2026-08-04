#ifndef LUA_RUNTIME_TASK_H
#define LUA_RUNTIME_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "lua_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUA_RUNTIME_STATE_IDLE = 0,
    LUA_RUNTIME_STATE_START_REQUESTED,
    LUA_RUNTIME_STATE_STARTING,
    LUA_RUNTIME_STATE_RUNNING,
    LUA_RUNTIME_STATE_RESTART_REQUESTED,
    LUA_RUNTIME_STATE_RESTARTING,
    LUA_RUNTIME_STATE_STOP_REQUESTED,
    LUA_RUNTIME_STATE_STOPPING,
    LUA_RUNTIME_STATE_ERROR
} LuaRuntimeState;

typedef enum {
    LUA_RUNTIME_ERROR_NONE = 0,
    LUA_RUNTIME_ERROR_INVALID_PATH,
    LUA_RUNTIME_ERROR_PATH_TOO_LONG,
    LUA_RUNTIME_ERROR_BUSY,
    LUA_RUNTIME_ERROR_INIT_FAILED,
    LUA_RUNTIME_ERROR_CALLBACK_FAILED,
    LUA_RUNTIME_ERROR_INTERNAL
} LuaRuntimeError;

/*
 * Lua and LVGL share the app task. Call these APIs only from that task
 * (including LVGL callbacks), then call LuaRuntimeTask_Process() once per
 * app-loop iteration.
 */
bool LuaRuntimeTask_RequestStart(const char *cart_path);
void LuaRuntimeTask_RequestStop(void);
bool LuaRuntimeTask_RequestRestart(void);
void LuaRuntimeTask_Process(uint32_t now_ms);

bool LuaRuntimeTask_IsRunning(void);
bool LuaRuntimeTask_IsIdle(void);
bool LuaRuntimeTask_IsStopping(void);
bool LuaRuntimeTask_HasError(void);
LuaRuntimeState LuaRuntimeTask_GetState(void);
const char *LuaRuntimeTask_GetStateName(LuaRuntimeState state);
const char *LuaRuntimeTask_GetCurrentCartPath(void);
LuaRuntimeError LuaRuntimeTask_GetLastError(void);
const char *LuaRuntimeTask_GetLastErrorMessage(void);
const LuaRuntimeErrorInfo *LuaRuntimeTask_GetErrorInfo(void);
#if PERF_MONITOR_ENABLE
bool LuaRuntimeTask_DebugStartSource(const char *source,
                                     const char *chunk_name);
#endif

#ifdef __cplusplus
}
#endif

#endif
