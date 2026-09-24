#include "lua_execution_budget.h"

#include "lauxlib.h"

#include <stdio.h>
#include <string.h>

#if defined(STM32H743xx)
#include "stm32h7xx.h"
#else
#include <time.h>
#endif

#define LUA_EXEC_SCOPE_DEPTH_MAX 4u

typedef struct {
  lua_State* vm;
  LuaRuntimeErrorStage stage;
  uint32_t owner_id;
  uint32_t generation;
  uint32_t budget_us;
  uint32_t hook_count;
  uint32_t elapsed_us;
  uint32_t pause_depth;
  uint64_t start;
  bool active;
  bool expired;
} LuaExecutionBudgetScope;

static LuaExecutionBudgetScope s_scope;
static LuaExecutionBudgetScope s_parent_scopes[LUA_EXEC_SCOPE_DEPTH_MAX];
static uint32_t s_scope_depth;
static int s_timeout_error_ref = LUA_NOREF;

static void budget_clock_init(void) {
#if defined(STM32H743xx)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }
#endif
}

static const char* budget_stage_name(LuaRuntimeErrorStage stage) {
  switch (stage) {
    case LUA_RUNTIME_ERROR_STAGE_LOAD: return "load";
    case LUA_RUNTIME_ERROR_STAGE_INIT: return "init";
    case LUA_RUNTIME_ERROR_STAGE_UPDATE: return "update";
    case LUA_RUNTIME_ERROR_STAGE_INPUT: return "input";
    case LUA_RUNTIME_ERROR_STAGE_TIMER: return "timer";
    case LUA_RUNTIME_ERROR_STAGE_MESSAGE: return "message";
    case LUA_RUNTIME_ERROR_STAGE_FINAL: return "final";
    case LUA_RUNTIME_ERROR_STAGE_NONE:
    default:
      return "none";
  }
}

static uint64_t budget_clock_now(void) {
#if defined(STM32H743xx)
  return (uint64_t)DWT->CYCCNT;
#else
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
#endif
}

static uint32_t budget_elapsed_us(uint64_t start, uint64_t end) {
#if defined(STM32H743xx)
  uint32_t cycles = (uint32_t)end - (uint32_t)start;
  uint32_t cycles_per_us = SystemCoreClock / UINT32_C(1000000);
  if (cycles_per_us == 0u) cycles_per_us = 1u;
  return cycles / cycles_per_us;
#else
  uint64_t elapsed_ns = end - start;
  uint64_t elapsed_us = elapsed_ns / UINT64_C(1000);
  return elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
#endif
}

static uint32_t budget_add_elapsed(uint32_t accumulated, uint32_t delta) {
  return delta > UINT32_MAX - accumulated ? UINT32_MAX : accumulated + delta;
}

static uint32_t budget_scope_elapsed(void) {
  uint32_t elapsed_us = s_scope.elapsed_us;
  if (s_scope.active && s_scope.pause_depth == 0u) {
    elapsed_us = budget_add_elapsed(
        elapsed_us, budget_elapsed_us(s_scope.start, budget_clock_now()));
  }
  return elapsed_us;
}

static void budget_hook(lua_State* L, lua_Debug* ar) {
  (void)ar;
  if (!s_scope.active || s_scope.expired) return;

  ++s_scope.hook_count;
  uint32_t elapsed_us = budget_scope_elapsed();
  if (elapsed_us <= s_scope.budget_us) return;

  s_scope.expired = true;
  lua_rawgeti(L, LUA_REGISTRYINDEX, s_timeout_error_ref);
  (void)lua_error(L);
}

uint32_t LuaExecutionBudget_ForStage(LuaRuntimeErrorStage stage) {
  switch (stage) {
    case LUA_RUNTIME_ERROR_STAGE_LOAD: return LUA_EXEC_LOAD_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_INIT: return LUA_EXEC_INIT_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_INPUT: return LUA_EXEC_INPUT_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_TIMER: return LUA_EXEC_TIMER_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_MESSAGE: return LUA_EXEC_MESSAGE_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_FINAL: return LUA_EXEC_FINAL_BUDGET_US;
    case LUA_RUNTIME_ERROR_STAGE_UPDATE:
    case LUA_RUNTIME_ERROR_STAGE_NONE:
    default:
      return LUA_EXEC_UPDATE_BUDGET_US;
  }
}

void LuaExecutionBudget_Install(lua_State* L) {
  LuaExecutionBudget_Reset();
  if (L == NULL) return;

  budget_clock_init();
  lua_pushliteral(L, "Lua execution budget exceeded");
  s_timeout_error_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_sethook(L, budget_hook, LUA_MASKCOUNT,
              LUA_EXEC_HOOK_INSTRUCTION_INTERVAL);
}

void LuaExecutionBudget_Reset(void) {
  memset(&s_scope, 0, sizeof(s_scope));
  memset(s_parent_scopes, 0, sizeof(s_parent_scopes));
  s_scope_depth = 0u;
  s_timeout_error_ref = LUA_NOREF;
}

void LuaExecutionBudget_Begin(lua_State* L,
                              LuaRuntimeErrorStage stage,
                              uint32_t owner_id,
                              uint32_t generation) {
  if (s_scope_depth < LUA_EXEC_SCOPE_DEPTH_MAX) {
    s_parent_scopes[s_scope_depth] = s_scope;
    ++s_scope_depth;
  }

  memset(&s_scope, 0, sizeof(s_scope));
  s_scope.vm = L;
  s_scope.stage = stage;
  s_scope.owner_id = owner_id;
  s_scope.generation = generation;
  s_scope.budget_us = LuaExecutionBudget_ForStage(stage);
  s_scope.start = budget_clock_now();
  s_scope.active = L != NULL;
}

void LuaExecutionBudget_PauseForBlockingCall(void) {
  if (!s_scope.active) return;
  if (s_scope.pause_depth == 0u) {
    s_scope.elapsed_us = budget_add_elapsed(
        s_scope.elapsed_us,
        budget_elapsed_us(s_scope.start, budget_clock_now()));
    if (s_scope.elapsed_us > s_scope.budget_us) s_scope.expired = true;
  }
  ++s_scope.pause_depth;
}

void LuaExecutionBudget_ResumeAfterBlockingCall(void) {
  if (!s_scope.active || s_scope.pause_depth == 0u) return;
  if (--s_scope.pause_depth == 0u) s_scope.start = budget_clock_now();
}

bool LuaExecutionBudget_End(LuaExecutionBudgetReport* report) {
  uint32_t elapsed_us = 0u;
  bool expired = false;

  if (s_scope.active) {
    elapsed_us = budget_scope_elapsed();
    expired = s_scope.expired || elapsed_us > s_scope.budget_us;
  }

  if (expired && report != NULL) {
    report->stage = s_scope.stage;
    report->owner_id = s_scope.owner_id;
    report->generation = s_scope.generation;
    report->elapsed_us = elapsed_us;
    report->budget_us = s_scope.budget_us;
    report->hook_count = s_scope.hook_count;
    report->instruction_interval = LUA_EXEC_HOOK_INSTRUCTION_INTERVAL;
  }

  memset(&s_scope, 0, sizeof(s_scope));
  if (s_scope_depth > 0u) {
    --s_scope_depth;
    s_scope = s_parent_scopes[s_scope_depth];
    memset(&s_parent_scopes[s_scope_depth], 0,
           sizeof(s_parent_scopes[s_scope_depth]));
  }
  return expired;
}

void LuaExecutionBudget_Format(const LuaExecutionBudgetReport* report,
                               char* buffer,
                               size_t buffer_size) {
  if (buffer == NULL || buffer_size == 0u) return;
  if (report == NULL) {
    (void)snprintf(buffer, buffer_size, "Lua execution budget exceeded");
    return;
  }
  (void)snprintf(buffer, buffer_size,
                 "Lua execution budget exceeded: stage=%s owner=%lu generation=%lu "
                 "elapsed_us=%lu budget_us=%lu hook_count=%lu interval=%lu",
                 budget_stage_name(report->stage),
                 (unsigned long)report->owner_id,
                 (unsigned long)report->generation,
                 (unsigned long)report->elapsed_us,
                 (unsigned long)report->budget_us,
                 (unsigned long)report->hook_count,
                 (unsigned long)report->instruction_interval);
}
