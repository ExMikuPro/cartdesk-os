# CartDesk-OS 稳定性验证

## 修复范围

本轮只处理 `random.integer()` 边界、storage integer 边界、Lua runtime 错误传播、
HostTest/CTest 与 owner 生命周期测试。未增加业务页面、Lua 模块或 RTOS 任务，也未修改
`.ioc`、链接脚本、SDRAM 布局、任务栈和 `configTOTAL_HEAP_SIZE`。

固件与 host `luavm` 现在统一使用 64 位 `lua_Integer`。这是极值 API 契约成立的前提；
旧 32 位 integer ABI 生成的 Lua bytecode 必须用当前 `luavm` 重新编译。

## random.integer 边界

`random.integer(min_value, max_value)` 包含两端，可选结果数量只允许 `1..2^32`。
实现先把端点转换为 `uint64_t` 再计算无符号距离，避免 `INT64_MIN/INT64_MAX` 的有符号
溢出。距离大于 `UINT32_MAX` 时返回：

```lua
nil, "random range exceeds 32-bit entropy"
```

距离小于 `UINT32_MAX` 时使用 rejection sampling；跨度恰为 `2^32` 时直接使用完整
`uint32_t`，不会执行模零。host 构建可注入固定 `uint32_t` provider，生产固件仍只调用
`RNG_GetU32()`。

`lua_random_test` 覆盖单值、2、10、`2^16`、`2^32-1`、`2^32`、超范围、两端
Lua integer、参数类型、RNG 失败和 rejection 重试，并启用 ASan/UBSan。

## storage integer 边界

磁盘格式仍为有符号 32 位小端。`storage.set()` 仅接受
`INT32_MIN..INT32_MAX`；越界返回：

```lua
nil, "integer value is outside int32 range"
```

检查发生在 pending store 变更前，因此失败不会覆盖旧值。storage header/payload 的
小端编码和 CRC 由 `cart_storage_format.c` 在固件 IO service 与 host test 间共享，
没有复制第二套算法。测试覆盖边界、越界不污染、queue full、commit/reload、固定字节序、
header 格式损坏、payload CRC 损坏和旧合法数据读取。

## Lua init 错误传播

runtime 保存固定上限的 `stage/message/traceback/app_id/owner_id/cart_id/tick`。
load、init、update、input、timer、message、final 使用统一阶段。回调错误会停止 runtime，
失效 Foundation/UI owner，停止后续 callback 派发并清空输入/消息队列。
`LuaRuntimeTask_Process()` 在 app task 安全点把状态切到 `ERROR`；Launcher 再创建可退出的
错误页，不在 Lua C binding 栈中删除 runtime screen。

init 失败时不调用 `final()`：实例没有成功完成 init，owner 由错误清理路径回收一次。
已经完成 init 的应用正常退出时仍调用 `final()`；final 自身抛错会记录 `FINAL`，随后继续
完成 owner 和 VM 清理。

## Host Test 与 CTest

标准入口：

```bash
cmake --preset HostTest
cmake --build --preset HostTest
ctest --preset HostTest --output-on-failure
```

`HostTest` 强制使用 native compiler，不进入 ARM toolchain。当前注册 14 项：

- `xhgc_cart_host_test`
- `task_message_contract_test`
- `lua_runtime_task_test`
- `lua_ui_owner_test`
- `lua_foundation_owner_test`
- `lua_crc_test`
- `lua_random_test`
- `lua_storage_test`
- `lua_assets_test`
- `lua_timer_test`
- `lua_vm_lifecycle_test`
- `luavm_self_test`
- `lua_style_lint`
- `lua_syntax_compile`

项目自有边界测试启用 `-Wall -Wextra -Werror`；random/storage/assets/timer/lifecycle 还启用
ASan/UBSan，random/storage 的共享核心另启用 `-Wconversion -Wsign-conversion`。

## Owner 生命周期

Host 已验证：UI 子树创建/删除 1000 次及 LVGL create 失败；asset handle 1000 个、load
失败并防 double release；timer 创建/取消 1000 次、32 个上限失败及 callback error；
Foundation owner 100 次；init 失败/销毁 50 次；storage snapshot allocation/queue full、
100 个 pending IO owner 退出和 stale completion；错误后新应用可正常启动。

目标板 SizeDebug mailbox `g_stability_board_*` 只允许 GDB 写标量命令，Lua/LVGL 操作仍由
app task 执行。专用脚本位于 `tests/lua/stability_board_test.lua`。板上已完成 50 轮：
random 极值通过、创建 UI/timer、init 主动失败、错误页、owner 清理、返回 Launcher；
结果为 50 completed / 0 failures，最终 UI owner registry 四槽全部 inactive。

## GDB 调试方法

仓库唯一 OpenOCD 配置为 `CartDeck.cfg`：ST-Link、SWD、STM32H7 dual-bank、
`reset_config srst_only`、8 MHz。实际工具为 OpenOCD 0.12.0 与 GNU GDB 15.2.90。

```bash
openocd -f CartDeck.cfg
arm-none-eabi-gdb build/SizeDebug/cartdesk-os.elf
```

```gdb
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
break HardFault_Handler
break MemManage_Handler
break BusFault_Handler
break UsageFault_Handler
continue
```

目标启动完成后再 halt，设置 `g_stability_board_target` 和
`g_stability_board_command=1`，然后 continue。不要在 C runtime 清零 `.bss` 前写 mailbox。
STM32H743 提供 8 个硬件 breakpoint；`Error_Handler` 有 5 个 location，和四个 Fault 入口
同时设置会超过数量，应分批验证。

## Fault 捕获方法

Fault 断点命中后记录：

```gdb
info registers
bt
x/16wx $sp
p/x SCB->CFSR
p/x SCB->HFSR
p/x SCB->MMFAR
p/x SCB->BFAR
```

本轮没有为 random 人为制造 Fault。50 轮测试后以上四个 SCB fault 寄存器均为 0。
CrashRecord 受控 UDF smoke 未执行，避免将 random 验证和故障记录验证混在一起。

## 实机测试步骤

1. 构建并加载 `build/SizeDebug/cartdesk-os.elf`。
2. 保留四个 Fault handler hardware breakpoint。
3. 启动到 Launcher 后 halt，设置 target=50、command=1。
4. continue，等待 `g_stability_board_state` 回到 0。
5. 检查 completed=50、failures=0、last_stage=INIT、runtime=IDLE。
6. 在 `prv_show_runtime_error_screen` 断点检查 traceback、owner registry 和错误 UI。
7. 比较 FreeRTOS heap、任务栈高水位、stale completion 和 SCB fault 寄存器。

## 测试结果

- Host：14/14 通过，0 失败。
- Release：成功；Flash 549184 B，DTCM 69680 B，AXI RAM 365216 B，D2 4128 B。
- SizeDebug：成功；Flash 552076 B，DTCM 72968 B，AXI RAM 365216 B，D2 4128 B。
- 目标板：50/50，0 failure；Fault 寄存器全 0。
- FreeRTOS heap：free 22928 B，minimum-ever-free 22928 B，测试前后相同。
- 栈高水位：app 6910 words（约 27640 B），io 2623 words（约 10492 B），
  background 1322 words（约 5288 B）；audio 无命令，现有统计点未被执行，待确认。
- app stale completion=0；io failed/timeout/queue_full/stale_completion 均为 0。

## 尚未验证

- 未执行破坏性的 QFlash font smoke，也未人为触发 UDF 验证 CrashRecord 全链路。
- 未在真实 SD cart 上覆盖 storage commit 后立即拔卡/掉电；固定格式和 completion 生命周期
  已由 host 验证。
- 旧板载 `cart.bin` 是 32 位 Lua integer bytecode，需重新打包后才能作为正常应用回归。
- audio task 没有命令，无法从现有统计点取得真实栈高水位。
- 构建默认只生成 ELF/MAP，当前 CMake 未生成固件 BIN/HEX。
