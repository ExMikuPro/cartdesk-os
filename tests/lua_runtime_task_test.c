#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cart_log.h"
#include "lua_runtime_task.h"

static int s_stub_init_rc = 0;
static int s_stub_init_calls = 0;
static int s_stub_shutdown_calls = 0;
static int s_stub_update_calls = 0;
static char s_stub_last_init_path[256];
static bool s_stub_reports_callback_error = false;
static LuaRuntimeErrorInfo s_stub_error;

void CartLog_Write(cart_log_level_t level, const char *tag, const char *message)
{
    (void)level;
    (void)tag;
    (void)message;
}

bool lua_vm_get_runtime_error(LuaRuntimeErrorInfo *out_error)
{
    if (!s_stub_reports_callback_error) return false;
    if (out_error != NULL) *out_error = s_stub_error;
    return true;
}

int lua_init_from_cart(const char *cart_path)
{
    s_stub_init_calls += 1;
    if (cart_path == NULL) {
        s_stub_last_init_path[0] = '\0';
    } else {
        strncpy(s_stub_last_init_path, cart_path, sizeof(s_stub_last_init_path) - 1u);
        s_stub_last_init_path[sizeof(s_stub_last_init_path) - 1u] = '\0';
    }
    return s_stub_init_rc;
}

int lua_shutdown(void)
{
    s_stub_shutdown_calls += 1;
    return 0;
}

void lua_update_task(void)
{
    s_stub_update_calls += 1;
}

static void reset_stub_state(void)
{
    s_stub_init_rc = 0;
    s_stub_init_calls = 0;
    s_stub_shutdown_calls = 0;
    s_stub_update_calls = 0;
    s_stub_last_init_path[0] = '\0';
    s_stub_reports_callback_error = false;
    memset(&s_stub_error, 0, sizeof(s_stub_error));
}

static void drive_task_until_running(uint32_t now_ms)
{
    LuaRuntimeTask_Process(now_ms);
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_RUNNING);
}

static void drive_task_until_idle(uint32_t now_ms)
{
    LuaRuntimeTask_Process(now_ms);
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_IDLE);
}

int main(void)
{
    reset_stub_state();

    assert(LuaRuntimeTask_IsIdle());
    assert(!LuaRuntimeTask_IsRunning());
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_IDLE);
    assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_NONE);

    assert(!LuaRuntimeTask_RequestStart(NULL));
    assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_INVALID_PATH);
    assert(LuaRuntimeTask_IsIdle());

    {
        char long_path[300];
        memset(long_path, 'a', sizeof(long_path));
        long_path[sizeof(long_path) - 1u] = '\0';
        assert(!LuaRuntimeTask_RequestStart(long_path));
        assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_PATH_TOO_LONG);
        assert(LuaRuntimeTask_IsIdle());
    }

    assert(LuaRuntimeTask_RequestStart("0:/cart.bin"));
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_START_REQUESTED);
    assert(strcmp(LuaRuntimeTask_GetCurrentCartPath(), "0:/cart.bin") == 0);
    LuaRuntimeTask_RequestStop();
    assert(LuaRuntimeTask_IsIdle());
    assert(s_stub_shutdown_calls == 0);

    assert(LuaRuntimeTask_RequestStart("0:/cart.bin"));
    drive_task_until_running(100u);
    assert(s_stub_init_calls == 1);
    assert(strcmp(s_stub_last_init_path, "0:/cart.bin") == 0);
    assert(!LuaRuntimeTask_RequestStart("0:/other.bin"));
    assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_BUSY);
    assert(strcmp(LuaRuntimeTask_GetCurrentCartPath(), "0:/cart.bin") == 0);
    LuaRuntimeTask_Process(100u);
    assert(s_stub_update_calls == 1);
    LuaRuntimeTask_Process(109u);
    assert(s_stub_update_calls == 1);
    LuaRuntimeTask_Process(110u);
    assert(s_stub_update_calls == 2);
    assert(LuaRuntimeTask_RequestRestart());
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_RESTART_REQUESTED);
    LuaRuntimeTask_Process(111u);
    assert(LuaRuntimeTask_IsRunning());
    assert(s_stub_shutdown_calls == 1);
    assert(s_stub_init_calls == 2);
    LuaRuntimeTask_RequestStop();
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_STOP_REQUESTED);
    drive_task_until_idle(115u);
    assert(s_stub_shutdown_calls == 2);
    assert(LuaRuntimeTask_IsIdle());

    reset_stub_state();
    s_stub_init_rc = -1;
    assert(LuaRuntimeTask_RequestStart("0:/broken.bin"));
    LuaRuntimeTask_Process(200u);
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_ERROR);
    assert(LuaRuntimeTask_HasError());
    assert(!LuaRuntimeTask_IsRunning());
    assert(!LuaRuntimeTask_IsIdle());
    assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_INIT_FAILED);
    LuaRuntimeTask_Process(300u);
    assert(s_stub_update_calls == 0);
    LuaRuntimeTask_RequestStop();
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_STOP_REQUESTED);
    drive_task_until_idle(305u);
    assert(s_stub_shutdown_calls == 1);

    reset_stub_state();
    assert(LuaRuntimeTask_RequestStart("0:/init-error.bin"));
    drive_task_until_running(400u);
    s_stub_reports_callback_error = true;
    s_stub_error.stage = LUA_RUNTIME_ERROR_STAGE_INIT;
    s_stub_error.owner_id = 42u;
    s_stub_error.cart_id = UINT64_C(0x1234);
    strcpy(s_stub_error.app_id, "init-error-app");
    strcpy(s_stub_error.message, "intentional init failure");
    strcpy(s_stub_error.traceback, "init-error.lua:7: intentional init failure");
    LuaRuntimeTask_Process(400u);
    assert(LuaRuntimeTask_GetState() == LUA_RUNTIME_STATE_ERROR);
    assert(LuaRuntimeTask_GetLastError() == LUA_RUNTIME_ERROR_CALLBACK_FAILED);
    const LuaRuntimeErrorInfo *runtime_error = LuaRuntimeTask_GetErrorInfo();
    assert(runtime_error != NULL);
    assert(runtime_error->stage == LUA_RUNTIME_ERROR_STAGE_INIT);
    assert(runtime_error->owner_id == 42u);
    assert(strcmp(runtime_error->message, "intentional init failure") == 0);
    int updates_after_error = s_stub_update_calls;
    LuaRuntimeTask_Process(500u);
    assert(s_stub_update_calls == updates_after_error);
    LuaRuntimeTask_RequestStop();
    drive_task_until_idle(505u);

    reset_stub_state();
    assert(LuaRuntimeTask_RequestStart("0:/wrap.bin"));
    drive_task_until_running(UINT32_MAX - 3u);
    LuaRuntimeTask_Process(UINT32_MAX - 3u);
    assert(s_stub_update_calls == 1);
    LuaRuntimeTask_Process(5u);
    assert(s_stub_update_calls == 1);
    LuaRuntimeTask_Process(6u);
    assert(s_stub_update_calls == 2);
    LuaRuntimeTask_RequestStop();
    drive_task_until_idle(7u);

    return 0;
}
