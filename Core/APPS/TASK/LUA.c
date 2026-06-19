#include "lua_vm.h"
#include "cartdesk_task.h"

#include <stdio.h>
#include <string.h>

#define TASK_LUA_CART_PATH_MAX 256u

uint32_t TaskTicks_LUA = 0;

static volatile TaskLuaState s_lua_state = TASK_LUA_STATE_IDLE;
static volatile TaskLuaError s_lua_last_error = TASK_LUA_ERR_NONE;
static char s_lua_cart_path[TASK_LUA_CART_PATH_MAX];

static void Task_LUA_ClearTaskContext(void)
{
  s_lua_cart_path[0] = '\0';
  s_lua_last_error = TASK_LUA_ERR_NONE;
}

static const char *Task_LUA_ErrorMessage(TaskLuaError error)
{
  switch (error) {
    case TASK_LUA_ERR_NONE:
      return "none";
    case TASK_LUA_ERR_INVALID_PATH:
      return "invalid path";
    case TASK_LUA_ERR_PATH_TOO_LONG:
      return "path too long";
    case TASK_LUA_ERR_BUSY:
      return "busy";
    case TASK_LUA_ERR_INIT_FAILED:
      return "init failed";
    case TASK_LUA_ERR_SHUTDOWN_FAILED:
      return "shutdown failed";
    case TASK_LUA_ERR_INTERNAL:
    default:
      return "internal";
  }
}

static void Task_LUA_RecordError(TaskLuaError error, const char *context)
{
  s_lua_last_error = error;
  if (context != NULL && error != TASK_LUA_ERR_NONE) {
    printf("[lua-task] %s: %s\r\n", context, Task_LUA_ErrorMessage(error));
  }
}

bool Task_LUA_StartCart(const char *cart_path)
{
  size_t path_len;

  if (cart_path == NULL || cart_path[0] == '\0') {
    Task_LUA_RecordError(TASK_LUA_ERR_INVALID_PATH, "start rejected");
    return false;
  }

  if (s_lua_state != TASK_LUA_STATE_IDLE) {
    Task_LUA_RecordError(TASK_LUA_ERR_BUSY, "start rejected");
    return false;
  }

  path_len = strlen(cart_path);
  if (path_len >= sizeof(s_lua_cart_path)) {
    Task_LUA_RecordError(TASK_LUA_ERR_PATH_TOO_LONG, "start rejected");
    return false;
  }

  memcpy(s_lua_cart_path, cart_path, path_len + 1u);
  s_lua_last_error = TASK_LUA_ERR_NONE;
  s_lua_state = TASK_LUA_STATE_START_REQUESTED;
  TaskTicks_LUA = 0;
  return true;
}

void Task_LUA_Stop(void)
{
  switch (s_lua_state) {
    case TASK_LUA_STATE_IDLE:
    case TASK_LUA_STATE_STOP_REQUESTED:
    case TASK_LUA_STATE_STOPPING:
      return;

    case TASK_LUA_STATE_START_REQUESTED:
      Task_LUA_ClearTaskContext();
      s_lua_state = TASK_LUA_STATE_IDLE;
      TaskTicks_LUA = 50;
      return;

    case TASK_LUA_STATE_STARTING:
    case TASK_LUA_STATE_RUNNING:
    case TASK_LUA_STATE_ERROR:
      s_lua_state = TASK_LUA_STATE_STOP_REQUESTED;
      TaskTicks_LUA = 0;
      return;

    default:
      Task_LUA_RecordError(TASK_LUA_ERR_INTERNAL, "stop invalid state");
      s_lua_state = TASK_LUA_STATE_ERROR;
      TaskTicks_LUA = 0;
      return;
  }
}

bool Task_LUA_IsRunning(void)
{
  return s_lua_state == TASK_LUA_STATE_RUNNING;
}

bool Task_LUA_IsIdle(void)
{
  return s_lua_state == TASK_LUA_STATE_IDLE;
}

bool Task_LUA_IsStopping(void)
{
  return s_lua_state == TASK_LUA_STATE_STOP_REQUESTED ||
         s_lua_state == TASK_LUA_STATE_STOPPING;
}

bool Task_LUA_HasError(void)
{
  return s_lua_last_error != TASK_LUA_ERR_NONE;
}

TaskLuaState Task_LUA_GetState(void)
{
  return s_lua_state;
}

const char *Task_LUA_GetStateName(TaskLuaState state)
{
  switch (state) {
    case TASK_LUA_STATE_IDLE:
      return "IDLE";
    case TASK_LUA_STATE_START_REQUESTED:
      return "START_REQUESTED";
    case TASK_LUA_STATE_STARTING:
      return "STARTING";
    case TASK_LUA_STATE_RUNNING:
      return "RUNNING";
    case TASK_LUA_STATE_STOP_REQUESTED:
      return "STOP_REQUESTED";
    case TASK_LUA_STATE_STOPPING:
      return "STOPPING";
    case TASK_LUA_STATE_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

const char *Task_LUA_GetCurrentCartPath(void)
{
  return s_lua_cart_path;
}

int Task_LUA_GetLastError(void)
{
  return (int)s_lua_last_error;
}

const char *Task_LUA_GetLastErrorMessage(void)
{
  return Task_LUA_ErrorMessage(s_lua_last_error);
}

void Task_LUA(void)
{
  int init_rc;

  switch (s_lua_state) {
    case TASK_LUA_STATE_IDLE:
      TaskTicks_LUA = 50;
      return;

    case TASK_LUA_STATE_START_REQUESTED:
      s_lua_state = TASK_LUA_STATE_STARTING;
      init_rc = lua_init_from_cart(s_lua_cart_path);
      if (init_rc != 0) {
        Task_LUA_RecordError(TASK_LUA_ERR_INIT_FAILED, s_lua_cart_path[0] != '\0' ? s_lua_cart_path : "init failed");
        if (s_lua_state == TASK_LUA_STATE_STOP_REQUESTED) {
          TaskTicks_LUA = 0;
          return;
        }
        s_lua_state = TASK_LUA_STATE_ERROR;
        TaskTicks_LUA = 50;
        return;
      }

      if (s_lua_state == TASK_LUA_STATE_STOP_REQUESTED) {
        TaskTicks_LUA = 0;
        return;
      }

      s_lua_state = TASK_LUA_STATE_RUNNING;
      TaskTicks_LUA = 0;
      return;

    case TASK_LUA_STATE_STARTING:
      TaskTicks_LUA = 0;
      return;

    case TASK_LUA_STATE_RUNNING:
      if (TaskTicks_LUA == 0u) {
        lua_update_task();
        TaskTicks_LUA = 10;
      }
      return;

    case TASK_LUA_STATE_STOP_REQUESTED:
      s_lua_state = TASK_LUA_STATE_STOPPING;
      (void)lua_shutdown();
      Task_LUA_ClearTaskContext();
      s_lua_state = TASK_LUA_STATE_IDLE;
      TaskTicks_LUA = 50;
      return;

    case TASK_LUA_STATE_STOPPING:
      TaskTicks_LUA = 0;
      return;

    case TASK_LUA_STATE_ERROR:
      TaskTicks_LUA = 50;
      return;

    default:
      Task_LUA_RecordError(TASK_LUA_ERR_INTERNAL, "task invalid state");
      s_lua_state = TASK_LUA_STATE_ERROR;
      TaskTicks_LUA = 50;
      return;
  }
}
