#include "lua_runtime_task.h"

#include <stdio.h>
#include <string.h>

#include "lua_vm.h"
#include "cart_log.h"

#define LUA_RUNTIME_CART_PATH_MAX 256u
#define LUA_RUNTIME_UPDATE_PERIOD_MS 10u

static LuaRuntimeState s_state = LUA_RUNTIME_STATE_IDLE;
static LuaRuntimeError s_last_error = LUA_RUNTIME_ERROR_NONE;
static char s_cart_path[LUA_RUNTIME_CART_PATH_MAX];
static uint32_t s_next_update_ms;
static LuaRuntimeErrorInfo s_error_info;

static void record_error(LuaRuntimeError error, const char *context);

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void clear_context(void)
{
    s_cart_path[0] = '\0';
    s_last_error = LUA_RUNTIME_ERROR_NONE;
    s_next_update_ms = 0u;
    memset(&s_error_info, 0, sizeof(s_error_info));
}

static const char *error_message(LuaRuntimeError error)
{
    switch (error) {
        case LUA_RUNTIME_ERROR_NONE:
            return "none";
        case LUA_RUNTIME_ERROR_INVALID_PATH:
            return "invalid path";
        case LUA_RUNTIME_ERROR_PATH_TOO_LONG:
            return "path too long";
        case LUA_RUNTIME_ERROR_BUSY:
            return "busy";
        case LUA_RUNTIME_ERROR_INIT_FAILED:
            return "init failed";
        case LUA_RUNTIME_ERROR_CALLBACK_FAILED:
            return "callback failed";
        case LUA_RUNTIME_ERROR_INTERNAL:
        default:
            return "internal";
    }
}

static void record_vm_error(LuaRuntimeError fallback,
                            LuaRuntimeErrorStage fallback_stage,
                            const char *fallback_message)
{
    memset(&s_error_info, 0, sizeof(s_error_info));
    if (!lua_vm_get_runtime_error(&s_error_info)) {
        s_error_info.stage = fallback_stage;
        s_error_info.tick = s_next_update_ms;
        (void)snprintf(s_error_info.message, sizeof(s_error_info.message),
                       "%s", fallback_message != NULL
                                  ? fallback_message : error_message(fallback));
        (void)snprintf(s_error_info.traceback,
                       sizeof(s_error_info.traceback), "%s",
                       s_error_info.message);
    }
    record_error(fallback, s_error_info.message);
}

static void record_error(LuaRuntimeError error, const char *context)
{
    s_last_error = error;
    if (context != NULL && error != LUA_RUNTIME_ERROR_NONE) {
        char message[192];
        const char *suffix = error_message(error);
        size_t suffix_len = strlen(suffix);
        size_t context_limit = sizeof(message) - suffix_len - 3u;
        size_t context_len = strnlen(context, context_limit);
        memcpy(message, context, context_len);
        message[context_len] = ':';
        message[context_len + 1u] = ' ';
        memcpy(message + context_len + 2u, suffix, suffix_len + 1u);
        CartLog_Write(CART_LOG_ERROR, "lua-runtime", message);
    }
}

bool LuaRuntimeTask_RequestStart(const char *cart_path)
{
    size_t path_len;

    if (cart_path == NULL || cart_path[0] == '\0') {
        record_error(LUA_RUNTIME_ERROR_INVALID_PATH, "start rejected");
        return false;
    }
    if (s_state != LUA_RUNTIME_STATE_IDLE) {
        record_error(LUA_RUNTIME_ERROR_BUSY, "start rejected");
        return false;
    }

    path_len = strlen(cart_path);
    if (path_len >= sizeof(s_cart_path)) {
        record_error(LUA_RUNTIME_ERROR_PATH_TOO_LONG, "start rejected");
        return false;
    }

    memcpy(s_cart_path, cart_path, path_len + 1u);
    s_last_error = LUA_RUNTIME_ERROR_NONE;
    s_state = LUA_RUNTIME_STATE_START_REQUESTED;
    return true;
}

#if PERF_MONITOR_ENABLE
bool LuaRuntimeTask_DebugStartSource(const char *source,
                                     const char *chunk_name)
{
    if (source == NULL || chunk_name == NULL || s_state != LUA_RUNTIME_STATE_IDLE) {
        return false;
    }

    clear_context();
    (void)snprintf(s_cart_path, sizeof(s_cart_path), "%s", chunk_name);
    s_state = LUA_RUNTIME_STATE_STARTING;
    if (lua_init_from_source_for_stability_test(source, chunk_name) != 0) {
        record_vm_error(LUA_RUNTIME_ERROR_INIT_FAILED,
                        LUA_RUNTIME_ERROR_STAGE_LOAD,
                        "stability source load failed");
        s_state = LUA_RUNTIME_STATE_ERROR;
        return false;
    }

    s_next_update_ms = 0u;
    s_state = LUA_RUNTIME_STATE_RUNNING;
    return true;
}
#endif

void LuaRuntimeTask_RequestStop(void)
{
    switch (s_state) {
        case LUA_RUNTIME_STATE_IDLE:
        case LUA_RUNTIME_STATE_STOP_REQUESTED:
        case LUA_RUNTIME_STATE_STOPPING:
            return;

        case LUA_RUNTIME_STATE_START_REQUESTED:
            clear_context();
            s_state = LUA_RUNTIME_STATE_IDLE;
            return;

        case LUA_RUNTIME_STATE_STARTING:
        case LUA_RUNTIME_STATE_RUNNING:
        case LUA_RUNTIME_STATE_RESTART_REQUESTED:
        case LUA_RUNTIME_STATE_RESTARTING:
        case LUA_RUNTIME_STATE_ERROR:
            s_state = LUA_RUNTIME_STATE_STOP_REQUESTED;
            return;

        default:
            record_error(LUA_RUNTIME_ERROR_INTERNAL, "stop invalid state");
            s_state = LUA_RUNTIME_STATE_ERROR;
            return;
    }
}

bool LuaRuntimeTask_RequestRestart(void)
{
    if (s_state != LUA_RUNTIME_STATE_RUNNING || s_cart_path[0] == '\0') {
        record_error(LUA_RUNTIME_ERROR_BUSY, "restart rejected");
        return false;
    }
    s_state = LUA_RUNTIME_STATE_RESTART_REQUESTED;
    return true;
}

void LuaRuntimeTask_Process(uint32_t now_ms)
{
    int init_rc;

    switch (s_state) {
        case LUA_RUNTIME_STATE_IDLE:
        case LUA_RUNTIME_STATE_STARTING:
        case LUA_RUNTIME_STATE_RESTARTING:
        case LUA_RUNTIME_STATE_STOPPING:
        case LUA_RUNTIME_STATE_ERROR:
            return;

        case LUA_RUNTIME_STATE_START_REQUESTED:
            s_state = LUA_RUNTIME_STATE_STARTING;
            init_rc = lua_init_from_cart(s_cart_path);
            if (init_rc != 0) {
                record_vm_error(LUA_RUNTIME_ERROR_INIT_FAILED,
                                LUA_RUNTIME_ERROR_STAGE_LOAD,
                                "Cart ENTRY load failed");
                s_state = LUA_RUNTIME_STATE_ERROR;
                return;
            }
            s_next_update_ms = now_ms;
            s_state = LUA_RUNTIME_STATE_RUNNING;
            return;

        case LUA_RUNTIME_STATE_RUNNING:
            if (time_reached(now_ms, s_next_update_ms)) {
                lua_update_task();
                if (lua_vm_get_runtime_error(NULL)) {
                    record_vm_error(LUA_RUNTIME_ERROR_CALLBACK_FAILED,
                                    LUA_RUNTIME_ERROR_STAGE_UPDATE,
                                    "Lua callback failed");
                    s_state = LUA_RUNTIME_STATE_ERROR;
                    return;
                }
                s_next_update_ms = now_ms + LUA_RUNTIME_UPDATE_PERIOD_MS;
            }
            return;

        case LUA_RUNTIME_STATE_RESTART_REQUESTED:
            s_state = LUA_RUNTIME_STATE_RESTARTING;
            (void)lua_shutdown();
            init_rc = lua_init_from_cart(s_cart_path);
            if (init_rc != 0) {
                record_vm_error(LUA_RUNTIME_ERROR_INIT_FAILED,
                                LUA_RUNTIME_ERROR_STAGE_LOAD,
                                "Cart restart failed");
                s_state = LUA_RUNTIME_STATE_ERROR;
                return;
            }
            s_next_update_ms = now_ms;
            s_state = LUA_RUNTIME_STATE_RUNNING;
            return;

        case LUA_RUNTIME_STATE_STOP_REQUESTED:
            s_state = LUA_RUNTIME_STATE_STOPPING;
            (void)lua_shutdown();
            clear_context();
            s_state = LUA_RUNTIME_STATE_IDLE;
            return;

        default:
            record_error(LUA_RUNTIME_ERROR_INTERNAL, "process invalid state");
            s_state = LUA_RUNTIME_STATE_ERROR;
            return;
    }
}

bool LuaRuntimeTask_IsRunning(void)
{
    return s_state == LUA_RUNTIME_STATE_RUNNING;
}

bool LuaRuntimeTask_IsIdle(void)
{
    return s_state == LUA_RUNTIME_STATE_IDLE;
}

bool LuaRuntimeTask_IsStopping(void)
{
    return s_state == LUA_RUNTIME_STATE_STOP_REQUESTED ||
           s_state == LUA_RUNTIME_STATE_STOPPING;
}

bool LuaRuntimeTask_HasError(void)
{
    return s_last_error != LUA_RUNTIME_ERROR_NONE;
}

LuaRuntimeState LuaRuntimeTask_GetState(void)
{
    return s_state;
}

const char *LuaRuntimeTask_GetStateName(LuaRuntimeState state)
{
    switch (state) {
        case LUA_RUNTIME_STATE_IDLE:
            return "IDLE";
        case LUA_RUNTIME_STATE_START_REQUESTED:
            return "START_REQUESTED";
        case LUA_RUNTIME_STATE_STARTING:
            return "STARTING";
        case LUA_RUNTIME_STATE_RUNNING:
            return "RUNNING";
        case LUA_RUNTIME_STATE_RESTART_REQUESTED:
            return "RESTART_REQUESTED";
        case LUA_RUNTIME_STATE_RESTARTING:
            return "RESTARTING";
        case LUA_RUNTIME_STATE_STOP_REQUESTED:
            return "STOP_REQUESTED";
        case LUA_RUNTIME_STATE_STOPPING:
            return "STOPPING";
        case LUA_RUNTIME_STATE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

const char *LuaRuntimeTask_GetCurrentCartPath(void)
{
    return s_cart_path;
}

LuaRuntimeError LuaRuntimeTask_GetLastError(void)
{
    return s_last_error;
}

const char *LuaRuntimeTask_GetLastErrorMessage(void)
{
    return error_message(s_last_error);
}

const LuaRuntimeErrorInfo *LuaRuntimeTask_GetErrorInfo(void)
{
    return s_error_info.stage != LUA_RUNTIME_ERROR_STAGE_NONE
               ? &s_error_info : NULL;
}
