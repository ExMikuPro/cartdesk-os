//
// Created by Hatsune Miku on 2026/2/13.
//

#ifndef CARTDESK_TASK_H
#define CARTDESK_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include <main.h>

typedef enum {
    TASK_LUA_STATE_IDLE = 0,
    TASK_LUA_STATE_START_REQUESTED,
    TASK_LUA_STATE_STARTING,
    TASK_LUA_STATE_RUNNING,
    TASK_LUA_STATE_STOP_REQUESTED,
    TASK_LUA_STATE_STOPPING,
    TASK_LUA_STATE_ERROR
} TaskLuaState;

typedef enum {
    TASK_LUA_ERR_NONE = 0,
    TASK_LUA_ERR_INVALID_PATH,
    TASK_LUA_ERR_PATH_TOO_LONG,
    TASK_LUA_ERR_BUSY,
    TASK_LUA_ERR_INIT_FAILED,
    TASK_LUA_ERR_SHUTDOWN_FAILED,
    TASK_LUA_ERR_INTERNAL
} TaskLuaError;

extern uint32_t TaskTicks_LED;
extern uint32_t TaskTicks_LVGL;
extern uint32_t TaskTicks_LUA;

void Task_LED();
void Task_LVGL();
void Task_LUA();
bool Task_LUA_StartCart(const char *cart_path);
void Task_LUA_Stop(void);
bool Task_LUA_IsRunning(void);
bool Task_LUA_IsIdle(void);
bool Task_LUA_IsStopping(void);
bool Task_LUA_HasError(void);
TaskLuaState Task_LUA_GetState(void);
const char *Task_LUA_GetStateName(TaskLuaState state);
const char *Task_LUA_GetCurrentCartPath(void);
int Task_LUA_GetLastError(void);
const char *Task_LUA_GetLastErrorMessage(void);

#endif //CARTDESK_TASK_H
