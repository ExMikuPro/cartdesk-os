#include "lua_runtime_task.h"

#include <stdio.h>
#include <string.h>

#include "lua_vm.h"

#define LUA_RUNTIME_CART_PATH_MAX 256u
#define LUA_RUNTIME_UPDATE_PERIOD_MS 10u

static LuaRuntimeState s_state = LUA_RUNTIME_STATE_IDLE;
static LuaRuntimeError s_last_error = LUA_RUNTIME_ERROR_NONE;
static char s_cart_path[LUA_RUNTIME_CART_PATH_MAX];
static uint32_t s_next_update_ms;

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void clear_context(void)
{
    s_cart_path[0] = '\0';
    s_last_error = LUA_RUNTIME_ERROR_NONE;
    s_next_update_ms = 0u;
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
        case LUA_RUNTIME_ERROR_INTERNAL:
        default:
            return "internal";
    }
}

static void record_error(LuaRuntimeError error, const char *context)
{
    s_last_error = error;
    if (context != NULL && error != LUA_RUNTIME_ERROR_NONE) {
        printf("[lua-runtime] %s: %s\r\n", context, error_message(error));
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
        case LUA_RUNTIME_STATE_ERROR:
            s_state = LUA_RUNTIME_STATE_STOP_REQUESTED;
            return;

        default:
            record_error(LUA_RUNTIME_ERROR_INTERNAL, "stop invalid state");
            s_state = LUA_RUNTIME_STATE_ERROR;
            return;
    }
}

void LuaRuntimeTask_Process(uint32_t now_ms)
{
    int init_rc;

    switch (s_state) {
        case LUA_RUNTIME_STATE_IDLE:
        case LUA_RUNTIME_STATE_STARTING:
        case LUA_RUNTIME_STATE_STOPPING:
        case LUA_RUNTIME_STATE_ERROR:
            return;

        case LUA_RUNTIME_STATE_START_REQUESTED:
            s_state = LUA_RUNTIME_STATE_STARTING;
            init_rc = lua_init_from_cart(s_cart_path);
            if (init_rc != 0) {
                record_error(LUA_RUNTIME_ERROR_INIT_FAILED,
                             s_cart_path[0] != '\0' ? s_cart_path : "init failed");
                s_state = LUA_RUNTIME_STATE_ERROR;
                return;
            }
            s_next_update_ms = now_ms;
            s_state = LUA_RUNTIME_STATE_RUNNING;
            return;

        case LUA_RUNTIME_STATE_RUNNING:
            if (time_reached(now_ms, s_next_update_ms)) {
                lua_update_task();
                s_next_update_ms = now_ms + LUA_RUNTIME_UPDATE_PERIOD_MS;
            }
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
