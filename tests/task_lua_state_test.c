#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cartdesk_task.h"

static int s_stub_init_rc = 0;
static int s_stub_init_calls = 0;
static int s_stub_shutdown_calls = 0;
static int s_stub_update_calls = 0;
static char s_stub_last_init_path[256];

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

static void reset_stubs(void)
{
    s_stub_init_rc = 0;
    s_stub_init_calls = 0;
    s_stub_shutdown_calls = 0;
    s_stub_update_calls = 0;
    s_stub_last_init_path[0] = '\0';
}

static void drive_task_until_running(void)
{
    Task_LUA();
    assert(Task_LUA_GetState() == TASK_LUA_STATE_RUNNING);
}

static void drive_task_until_idle(void)
{
    Task_LUA();
    assert(Task_LUA_GetState() == TASK_LUA_STATE_IDLE);
}

int main(void)
{
    reset_stubs();

    assert(Task_LUA_IsIdle());
    assert(!Task_LUA_IsRunning());
    assert(Task_LUA_GetState() == TASK_LUA_STATE_IDLE);
    assert(Task_LUA_GetLastError() == TASK_LUA_ERR_NONE);

    assert(!Task_LUA_StartCart(NULL));
    assert(Task_LUA_GetLastError() == TASK_LUA_ERR_INVALID_PATH);
    assert(Task_LUA_IsIdle());

    {
        char long_path[300];
        memset(long_path, 'a', sizeof(long_path));
        long_path[sizeof(long_path) - 1u] = '\0';
        assert(!Task_LUA_StartCart(long_path));
        assert(Task_LUA_GetLastError() == TASK_LUA_ERR_PATH_TOO_LONG);
        assert(Task_LUA_IsIdle());
    }

    assert(Task_LUA_StartCart("0:/cart.bin"));
    assert(Task_LUA_GetState() == TASK_LUA_STATE_START_REQUESTED);
    assert(strcmp(Task_LUA_GetCurrentCartPath(), "0:/cart.bin") == 0);
    Task_LUA_Stop();
    assert(Task_LUA_IsIdle());
    assert(s_stub_shutdown_calls == 0);

    assert(Task_LUA_StartCart("0:/cart.bin"));
    drive_task_until_running();
    assert(s_stub_init_calls == 1);
    assert(strcmp(s_stub_last_init_path, "0:/cart.bin") == 0);
    assert(!Task_LUA_StartCart("0:/other.bin"));
    assert(Task_LUA_GetLastError() == TASK_LUA_ERR_BUSY);
    assert(strcmp(Task_LUA_GetCurrentCartPath(), "0:/cart.bin") == 0);
    TaskTicks_LUA = 0;
    Task_LUA();
    assert(s_stub_update_calls == 1);
    Task_LUA_Stop();
    assert(Task_LUA_GetState() == TASK_LUA_STATE_STOP_REQUESTED);
    drive_task_until_idle();
    assert(s_stub_shutdown_calls == 1);
    assert(Task_LUA_IsIdle());

    reset_stubs();
    s_stub_init_rc = -1;
    assert(Task_LUA_StartCart("0:/broken.bin"));
    Task_LUA();
    assert(Task_LUA_GetState() == TASK_LUA_STATE_ERROR);
    assert(Task_LUA_HasError());
    assert(!Task_LUA_IsRunning());
    assert(!Task_LUA_IsIdle());
    assert(Task_LUA_GetLastError() == TASK_LUA_ERR_INIT_FAILED);
    TaskTicks_LUA = 0;
    Task_LUA();
    assert(s_stub_update_calls == 0);
    Task_LUA_Stop();
    assert(Task_LUA_GetState() == TASK_LUA_STATE_STOP_REQUESTED);
    drive_task_until_idle();
    assert(s_stub_shutdown_calls == 1);

    return 0;
}
