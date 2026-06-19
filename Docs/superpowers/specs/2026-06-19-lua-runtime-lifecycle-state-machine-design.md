# Lua Runtime Lifecycle State Machine Design

## Goal

把 `cartdesk-os` 当前 Lua cart 的启动、运行、停止、错误和重复调用处理收敛为单一、可防御、可观测的生命周期状态机，避免多 flag 分散控制导致的半启动、重复关闭、误切屏和 launcher 误判。

## Scope

本次只覆盖 Lua runtime 生命周期边界稳定化：

- `Task_LUA_StartCart()` 启动请求防御
- `Task_LUA_Stop()` 幂等停止与取消启动
- `Task_LUA()` 中央状态转移
- launcher 启动/退出边界修正
- runtime stats 改为读取正式 Lua task 状态

本次不做：

- LVGL 性能优化
- error screen
- save/audio/input 重构
- cart/bin 格式变更
- resource manager 结构调整
- Lua API 扩展

## Current Context

当前实现位于 [Core/APPS/TASK/LUA.c](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/APPS/TASK/LUA.c:1)，主要由三个状态位驱动：

- `s_lua_ready`
- `s_lua_start_requested`
- `s_lua_stop_requested`

`Task_LUA_StartCart()` 当前只要未 ready 就接受启动请求，并直接记录路径指针；`Task_LUA_Stop()` 当前只置 stop flag；`Task_LUA()` 中再决定是否执行 `lua_init_from_cart()`、`lua_update_task()` 和 `lua_shutdown()`。

这会带来几个风险：

- 运行中重复 start 的处理不明确
- start 请求持有外部路径指针，不具备防御性
- init 失败后不会进入正式错误态
- launcher 当前“先切 runtime screen 再请求启动”，会在启动被拒绝时错误切屏
- stats 当前 `lua_runtime_state` 需要对齐正式 task 状态，而不是内部松散 flag

## Chosen Approach

采用集中式 task-level 状态机，状态定义在 `Core/APPS/TASK/LUA.h`，状态推进集中在 `Task_LUA()`，launcher 和 runtime stats 只通过只读 getter 观察状态。

不把状态机下沉到 `lua_vm.c`，避免扩大 Phase 2 范围；也不保留“flag 为真相、枚举为映射”的双轨模型，避免再次出现状态分裂。

## State Model

建议公开：

```c
typedef enum {
    TASK_LUA_STATE_IDLE = 0,
    TASK_LUA_STATE_START_REQUESTED,
    TASK_LUA_STATE_STARTING,
    TASK_LUA_STATE_RUNNING,
    TASK_LUA_STATE_STOP_REQUESTED,
    TASK_LUA_STATE_STOPPING,
    TASK_LUA_STATE_ERROR
} TaskLuaState;
```

状态语义固定如下：

- `IDLE`: 没有运行中的 Lua VM，也没有 pending start
- `START_REQUESTED`: 已接受启动请求，等待 `Task_LUA()` 执行 `lua_init_from_cart()`
- `STARTING`: 正在执行 `lua_init_from_cart()`，不接受新 start
- `RUNNING`: 只在这个状态调用 `lua_update_task()`
- `STOP_REQUESTED`: 已请求停止，等待 `Task_LUA()` 执行 cleanup
- `STOPPING`: 正在执行 `lua_shutdown()`
- `ERROR`: 启动或运行过程中出现错误；不 update，不自动清理，等待 `Task_LUA_Stop()`

## State Transitions

```text
IDLE
  -> START_REQUESTED
  -> STARTING
  -> RUNNING
  -> STOP_REQUESTED
  -> STOPPING
  -> IDLE

START_REQUESTED
  -> IDLE                 (Stop 取消启动)
  -> STARTING            (Task_LUA 消费请求)

STARTING
  -> RUNNING             (init 成功)
  -> ERROR               (init 失败)
  -> STOP_REQUESTED      (启动中收到 Stop，延后到 init 返回后统一清理)

RUNNING
  -> STOP_REQUESTED
  -> ERROR

STOP_REQUESTED
  -> STOPPING

STOPPING
  -> IDLE

ERROR
  -> STOP_REQUESTED
  -> STOPPING
  -> IDLE
```

明确约束：

- `ERROR` 下 `Task_LUA_IsRunning() == false`
- `ERROR` 下 `Task_LUA_IsIdle() == false`
- launcher 恢复只认 `Task_LUA_IsIdle()`
- `START_REQUESTED` 下 `Stop` 直接取消启动，不调用 `lua_shutdown()`
- `ERROR` 下 `Stop` 必须走统一 cleanup，即 `STOP_REQUESTED -> STOPPING -> IDLE`

## Public API

整理或新增这些接口，放在 `Core/APPS/TASK/LUA.h`：

```c
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
```

兼容原则：

- `Task_LUA_IsRunning()` 保持旧 launcher 语义，只在 `RUNNING` 返回 true
- getter 只读，不推进状态
- cart path 存在 task 模块自己的静态缓冲里，不暴露外部临时指针
- state name / error message 返回静态字符串

## Error Model

建议定义：

```c
typedef enum {
    TASK_LUA_ERR_NONE = 0,
    TASK_LUA_ERR_INVALID_PATH,
    TASK_LUA_ERR_PATH_TOO_LONG,
    TASK_LUA_ERR_BUSY,
    TASK_LUA_ERR_INIT_FAILED,
    TASK_LUA_ERR_SHUTDOWN_FAILED,
    TASK_LUA_ERR_INTERNAL
} TaskLuaError;
```

本次明确实现：

- `NONE`
- `INVALID_PATH`
- `PATH_TOO_LONG`
- `BUSY`
- `INIT_FAILED`
- `INTERNAL`

`SHUTDOWN_FAILED` 先保留枚举和字符串，不伪造失败，因为当前 `lua_shutdown()` 返回 0 且没有细粒度错误语义。

## Start Behavior

`Task_LUA_StartCart()` 将变为防御式请求入口：

- `NULL` 或空路径：拒绝，记录 `INVALID_PATH`
- 路径过长：拒绝，记录 `PATH_TOO_LONG`
- 非 `IDLE`：拒绝，记录 `BUSY`
- 成功时安全复制到 `g_current_cart_path`，清空上次错误，进入 `START_REQUESTED`

关键点：

- 不在 UI callback 里做 Lua 初始化
- 重复 start 不覆盖当前 cart path
- 成功后由 `Task_LUA()` 异步消费请求

## Stop Behavior

`Task_LUA_Stop()` 做成幂等入口：

- `IDLE` / `STOP_REQUESTED` / `STOPPING`: 直接返回
- `START_REQUESTED`: 取消 pending start，清空当前路径和错误上下文，回到 `IDLE`
- `STARTING` / `RUNNING` / `ERROR`: 进入 `STOP_REQUESTED`
- 未知状态：落到 `ERROR` 并记录 `INTERNAL`

特别说明：

- `STARTING` 期间的 stop 不硬中断 `lua_init_from_cart()`，只在其返回后由 `Task_LUA()` 接续 cleanup
- `ERROR` 不自动清理，必须显式 stop 后统一走 `lua_shutdown()`

## Task Loop Behavior

`Task_LUA()` 成为唯一状态推进点：

- `IDLE`: 直接返回
- `START_REQUESTED`: 切到 `STARTING`，执行 `lua_init_from_cart()`
- `STARTING`: 不单独停留多帧；只作为进入 init 前的正式状态
- init 成功：
  - 若期间未收到 stop，则进 `RUNNING`
  - 若期间收到 stop，则进 `STOP_REQUESTED`
- init 失败：
  - 记录 `INIT_FAILED`
  - 进入 `ERROR`
- `RUNNING`: 仅此状态调用 `lua_update_task()`
- `STOP_REQUESTED`: 切到 `STOPPING`
- `STOPPING`: 调 `lua_shutdown()`，清理 task 持有的 path/error/flags，回 `IDLE`
- `ERROR`: 不调用 `lua_update_task()`，等待 `Stop`

## Init Failure Policy

选择保留错误态：

```text
STARTING -> ERROR
ERROR --Task_LUA_Stop()--> STOP_REQUESTED -> STOPPING -> IDLE
```

原因：

- 可观测性更强，便于后续 Phase 3 error screen 直接消费
- 不会在失败瞬间丢失 `current_cart_path` / `last_error`
- launcher 不会把 `ERROR` 误判为“已退出”

## Queue and Cleanup Policy

当前 `lua_shutdown()` 已确认会：

- `res_scene_reset()`
- `lua_close(g_L)`
- 清空 input queue 计数与头尾
- 清空 message queue 计数与头尾
- 清理 runtime 标志

因此本次不重复在 task 层手工清空 queue，只保证所有正式关闭都统一经过 `STOPPING -> lua_shutdown()`。

`START_REQUESTED` 被取消时不调用 `lua_shutdown()`，因为此时 VM 还未启动。

## Launcher Integration

launcher 只做最小改动：

- 启动时先调 `Task_LUA_StartCart("0:/cart.bin")`
- 只有返回 true 才切到 runtime screen
- 返回 false 时只保留短日志或原有 status text，不切屏、不显示错误页
- 退出按钮继续只调 `Task_LUA_Stop()`
- 恢复 launcher 判断改为优先 `Task_LUA_IsIdle()`

这能避免：

- 启动被拒绝后误切 runtime screen
- `ERROR` 被当作“已经完全退出”

## Runtime Stats Integration

不新增 stats 字段。

只调整：

- `runtime_stats` 中 `lua_runtime_state` 的数值来源改为 `Task_LUA_GetState()`
- `lua_runtime_state` 的名字来源可直接复用 `Task_LUA_GetStateName()`

这样 runtime stats 继续可用，但状态语义与 task 生命周期一致。

## Concurrency Assumption

本次实现基于当前工程实际调用方式的保守假设：

- `Task_LUA()` 在图形主循环上下文里周期调用
- `Task_LUA_StartCart()` / `Task_LUA_Stop()` 由 UI callback 调用

如果这些调用都在同一 LVGL task 上下文，则无需引入复杂同步；如存在跨上下文风险，只使用项目现有轻量风格保护共享状态，不引入阻塞等待、动态分配或递归。

## Testing Plan

最低验证目标：

1. 上电 `IDLE`
2. 正常启动：`IDLE -> START_REQUESTED -> STARTING -> RUNNING`
3. 正常退出：`RUNNING -> STOP_REQUESTED -> STOPPING -> IDLE`
4. `IDLE` 下 `Stop` 无副作用
5. `RUNNING` 下重复 `StartCart` 返回 false
6. `START_REQUESTED` 后立刻 `Stop` 可取消启动
7. 空路径返回 `INVALID_PATH`
8. 过长路径返回 `PATH_TOO_LONG`
9. init 失败不进入 `RUNNING`
10. `ERROR` 下 `Stop` 能清理回 `IDLE`
11. 退出后 Lua heap / resource / queue 清空
12. launcher 不在 `ERROR` 或未清理完成时误恢复

## Compatibility

本次必须保持不变：

- `osDelay(5)`
- LVGL 主循环顺序
- display flush 行为
- Lua 脚本 API
- cart/bin 格式
- resource manager 释放语义
- stats 基础采集能力

## Next Phase

下一阶段建议是 `runtime error screen`：

- 直接消费 `TaskLuaState == ERROR`
- 展示 `last_error`
- 展示 `current_cart_path`

但本次不实现。
