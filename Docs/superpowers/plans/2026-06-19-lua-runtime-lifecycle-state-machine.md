# Lua Runtime Lifecycle State Machine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Lua cart 生命周期改成单一状态机，保证重复 Start/Stop、启动失败和 launcher 边界都可防御且可观测。

**Architecture:** 状态枚举、错误码、路径和最后错误统一由 task 层持有，`Task_LUA()` 是唯一允许推进状态流转的地方。launcher 和 runtime stats 只通过只读 getter 观察状态，不再直接推断内部 flag。

**Tech Stack:** C11、STM32 firmware task loop、LVGL launcher、现有 `lua_vm` runtime、CMake/Ninja、host-side `cc` 轻量测试桩。

---

### Task 1: Add a failing lifecycle API test

**Files:**
- Create: `tests/stubs/main.h`
- Create: `tests/task_lua_state_test.c`
- Test: `tests/task_lua_state_test.c`

- [ ] **Step 1: Write the failing test**

Add a host-side test that expects the new lifecycle API and state semantics:

```c
assert(Task_LUA_GetState() == TASK_LUA_STATE_IDLE);
assert(Task_LUA_StartCart("0:/cart.bin") == true);
assert(Task_LUA_GetState() == TASK_LUA_STATE_START_REQUESTED);
Task_LUA_Stop();
assert(Task_LUA_IsIdle() == true);
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cc -std=c11 -Itests/stubs -ICore/APPS/TASK -ICore/Inc tests/task_lua_state_test.c Core/APPS/TASK/LUA.c -o /tmp/task_lua_state_test
```

Expected: compile failure because `TaskLuaState` and the new getters do not exist yet.

- [ ] **Step 3: Keep the test as the contract**

Do not relax the test to match the old three-flag implementation. Keep it targeting the new API and behavior.

### Task 2: Implement the task-level state machine

**Files:**
- Modify: `Core/APPS/TASK/cartdesk_task.h`
- Modify: `Core/APPS/TASK/LUA.c`
- Test: `tests/task_lua_state_test.c`

- [ ] **Step 1: Add public enums and getters**

Expose `TaskLuaState`, `TaskLuaError`, `Task_LUA_GetState()`, `Task_LUA_GetStateName()`, `Task_LUA_GetCurrentCartPath()`, `Task_LUA_GetLastError()`, and `Task_LUA_GetLastErrorMessage()` in `cartdesk_task.h`.

- [ ] **Step 2: Replace loose flags with a single state source**

Move `Core/APPS/TASK/LUA.c` to:

```c
static volatile TaskLuaState s_lua_state = TASK_LUA_STATE_IDLE;
static volatile TaskLuaError s_lua_last_error = TASK_LUA_ERR_NONE;
static char s_lua_cart_path[256];
```

and add helpers for recording/clearing errors and resetting task-owned context.

- [ ] **Step 3: Implement defensive StartCart/Stop**

`Task_LUA_StartCart()` should validate path, reject non-`IDLE`, copy the path safely, clear the previous error, and move to `START_REQUESTED`.

`Task_LUA_Stop()` should:

```c
case TASK_LUA_STATE_START_REQUESTED:
    clear pending start;
    state = TASK_LUA_STATE_IDLE;
    return;
case TASK_LUA_STATE_STARTING:
case TASK_LUA_STATE_RUNNING:
case TASK_LUA_STATE_ERROR:
    state = TASK_LUA_STATE_STOP_REQUESTED;
    return;
```

- [ ] **Step 4: Centralize state transitions in Task_LUA()**

Only call `lua_update_task()` in `RUNNING`, only call `lua_shutdown()` in `STOPPING`, and retain `ERROR` until `Task_LUA_Stop()` drives cleanup.

- [ ] **Step 5: Run the host-side lifecycle test**

Run:

```bash
cc -std=c11 -Itests/stubs -ICore/APPS/TASK -ICore/Inc tests/task_lua_state_test.c Core/APPS/TASK/LUA.c -o /tmp/task_lua_state_test && /tmp/task_lua_state_test
```

Expected: test exits `0`.

### Task 3: Integrate launcher and runtime stats with the formal state

**Files:**
- Modify: `Core/Screen/Page/ui_screen_launcher.c`
- Modify: `Core/Debug/runtime_stats.c`
- Modify: `README.md`
- Test: firmware build

- [ ] **Step 1: Fix launcher start/stop boundaries**

Change launcher start flow to:

```c
if (!Task_LUA_StartCart("0:/cart.bin")) {
    prv_set_status_text("App cannot start");
    return;
}
prv_show_runtime_screen();
```

Use `Task_LUA_IsIdle()` instead of `!Task_LUA_IsRunning()` where recovery semantics require “fully exited”.

- [ ] **Step 2: Route runtime stats through the task state**

Replace direct `lua_vm_runtime_state()` reads with `Task_LUA_GetState()`, and make `RuntimeStats_LuaStateName()` delegate to `Task_LUA_GetStateName()`.

- [ ] **Step 3: Update docs for the new lifecycle semantics**

Document that launcher now waits for `Task_LUA_StartCart()` success before switching screens, and that runtime stats read the formal Lua task state.

- [ ] **Step 4: Build firmware and confirm success**

Run:

```bash
cmake --build --preset Debug -j8
```

Expected: build succeeds.
