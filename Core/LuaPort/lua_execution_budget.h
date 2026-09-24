#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lua.h"
#include "lua_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_EXEC_HOOK_INSTRUCTION_INTERVAL 1000
#define LUA_EXEC_LOAD_BUDGET_US             50000u
#define LUA_EXEC_INIT_BUDGET_US             50000u
#define LUA_EXEC_UPDATE_BUDGET_US           20000u
#define LUA_EXEC_INPUT_BUDGET_US            20000u
#define LUA_EXEC_TIMER_BUDGET_US            20000u
#define LUA_EXEC_MESSAGE_BUDGET_US          20000u
#define LUA_EXEC_FINAL_BUDGET_US            20000u

typedef struct {
  LuaRuntimeErrorStage stage;
  uint32_t owner_id;
  uint32_t generation;
  uint32_t elapsed_us;
  uint32_t budget_us;
  uint32_t hook_count;
  uint32_t instruction_interval;
} LuaExecutionBudgetReport;

void LuaExecutionBudget_Install(lua_State* L);
void LuaExecutionBudget_Reset(void);
void LuaExecutionBudget_Begin(lua_State* L,
                              LuaRuntimeErrorStage stage,
                              uint32_t owner_id,
                              uint32_t generation);
void LuaExecutionBudget_PauseForBlockingCall(void);
void LuaExecutionBudget_ResumeAfterBlockingCall(void);
bool LuaExecutionBudget_End(LuaExecutionBudgetReport* report);
uint32_t LuaExecutionBudget_ForStage(LuaRuntimeErrorStage stage);
void LuaExecutionBudget_Format(const LuaExecutionBudgetReport* report,
                               char* buffer,
                               size_t buffer_size);

#ifdef __cplusplus
}
#endif
