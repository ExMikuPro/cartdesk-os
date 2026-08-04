# CartDesk-OS 稳定性验证报告

## 1. Git 与构建环境

- 基线：`main`，HEAD `e35ea46`，开始时与 `origin/main` 同步。
- 修改前工作区：仅有用户未跟踪文件 `PROJECT_PROGRESS.md`、
  `PROJECT_PROGRESS_LATEST.md`；本轮未修改或覆盖它们。
- 编译器：GNU Tools for STM32 14.3.1；CMake 4.3.1。
- 调试器：OpenOCD 0.12.0，GNU GDB 15.2.90，ST-Link V2 / SWD 8 MHz。
- 未修改 `.ioc`、链接脚本、SDRAM 布局、任务栈或 98304 B FreeRTOS heap。

## 2. random.integer 修复

状态：host 已验证、GDB 实机已验证。

原实现先做可能溢出的有符号端点减法，再截为 `uint32_t`；跨度 `2^32` 会形成
`range==0` 并可能取模除零。新实现以无符号端点距离判断 inclusive span，拒绝大于
`2^32` 的范围，普通跨度 rejection sampling，`2^32` 直接映射完整 RNG 值。
测试专用 provider 可以固定随机序列和注入失败，生产路径仍为 `RNG_GetU32()`。

Host 的 ASan/UBSan 边界测试覆盖任务要求的全部跨度、Lua integer 极值、错误参数、
RNG 失败和 rejection 重试。目标板 50 轮重复执行合法/非法极值，无 Fault，且每轮继续
进入预期 init error，证明 Lua 仍可运行。

## 3. storage integer 修复

状态：host 已验证；真实 QFlash 掉电场景尚未验证。

`storage.set()` 现在只接受 `INT32_MIN..INT32_MAX`，越界在 pending mutation 前返回固定
错误，不截断、不饱和、不转 double。固件 IO 与 host 共用明确小端 header/payload 和
CRC helper。测试通过边界、旧值不污染、commit/reload、固定字节序、CRC 损坏与 queue
full；100 次 pending IO owner 退出后 completion 不访问旧 VM。

## 4. Lua init 错误传播

状态：host 已验证、GDB 实机已验证。

新增固定容量 error model，保存 stage、摘要、traceback、app/owner/cart/tick。
init/update/input/timer/message/final 均映射到真实阶段。回调错误停止 runtime 和后续派发，
owner 清理只执行一次；`LuaRuntimeTask` 在下一 app task 安全点进入 `ERROR`。Launcher
错误页显示“应用启动失败”、应用 ID、阶段、摘要和“返回 Launcher”。

GDB 在 `prv_show_runtime_error_screen` 入口读到 stage=INIT、owner_id=52、应用 ID
`stability_board_test.lua` 和完整 traceback；四个 UI owner 槽均 inactive。函数返回后
`s_runtime_error_visible=true`、runtime screen 非空、Launcher main container 已隐藏。

## 5. Host/CTest 修复

状态：host 已验证。

`HostTest` preset 使用 native compiler，并正式注册 14 项 CTest；过去 Release/SizeDebug
中的 0 tests 不再是项目测试入口。缺失 TASK include、CartLog/RTOS/RNG/FatFs/LVGL/
storage stub 已放到 tests 目录，生产代码未用宏绕开错误。`luavm_tool` 独立 Release 构建
也已恢复。

标准命令最终结果：配置成功、构建成功、14/14 通过、0 失败。

## 6. Owner 生命周期测试

状态：host 压力已验证；目标板 init-failure 循环已验证。

- UI：1000 次子树 handle 创建/递归失效，并注入 LVGL create 失败。
- asset：1000 handle，owner destroy 精确释放，无 double release。
- timer：1000 次创建/取消、32 个上限失败；一次性 timer callback error 的 registry
  引用已修复。
- Foundation：100 次 owner create/destroy。
- Lua VM：50 次 init fail + cleanup，随后正常应用可启动；另测 update/input/message/final。
- storage/IO：snapshot allocation/queue full，以及 100 次 pending owner 退出与 stale
  completion。
- 目标板：50 completed / 0 failures，最终 owner registry 全空、stale completion=0。

## 7. GDB 实机调试

状态：GDB 实机已验证。

使用仓库 `CartDeck.cfg` 和 SizeDebug ELF，完成 load/reset/halt/continue、真实符号断点与
状态读取。四个 Fault handler breakpoint 全程有效且未命中。专用 mailbox 只写标量，
所有 Lua/LVGL/owner 操作由 app task 执行，避免从 debugger context 破坏 owner 规则。

旧 SD `cart.bin` 的 ENTRY 是旧 32 位 Lua integer bytecode，当前 64 位 ABI 会在 load
阶段拒绝。实机稳定性因此使用 SizeDebug 内嵌测试源；该事实不是伪装成 init 失败通过，
旧 cart 必须由当前 `luavm` 重新打包。

## 8. RTOS Heap 与 Stack 数据

50 轮后：

| 指标 | 结果 |
|---|---:|
| FreeRTOS current free | 22928 B |
| FreeRTOS minimum-ever-free | 22928 B |
| app stack high-water | 6910 words / 约 27640 B |
| io stack high-water | 2623 words / 约 10492 B |
| background stack high-water | 1322 words / 约 5288 B |
| audio stack high-water | 0（无命令，统计点未执行） |
| app stale completion | 0 |
| io failed / timeout / queue full / stale | 0 / 0 / 0 / 0 |

FreeRTOS heap 在启动采样、循环中采样和循环结束采样均为 22928 B；未通过扩 heap 或栈
掩盖问题。

## 9. Fault 与 CrashRecord

状态：Fault 监控已验证；CrashRecord 受控触发尚未验证。

循环后 `CFSR/HFSR/MMFAR/BFAR` 全为 0。没有人为制造 Fault 来证明 random 修复，也未
运行会擦除 QFlash 字体区的 smoke。CrashRecord 的 RTC backup → reset → UART → SD
append → clear 本轮未执行，明确列为尚未验证。

## 10. 构建和测试结果

| 项目 | 结果 | Flash | DTCM | AXI RAM | D2 RAM |
|---|---|---:|---:|---:|---:|
| HostTest CTest | 14/14 PASS | - | - | - | - |
| Release | PASS | 549184 B | 69680 B | 365216 B | 4128 B |
| SizeDebug | PASS | 552076 B | 72968 B | 365216 B | 4128 B |

相对旧报告基线：Release Flash +5032 B、DTCM +1592 B；SizeDebug Flash +6636 B、
DTCM +1616 B。AXI/D2 未增长，FreeRTOS heap 配置未变。SizeDebug 额外增量包含仅调试
构建启用的板级压力脚本/mailbox。

构建没有新增 warning 类型；仍有既存 Launcher file-size `snprintf` 截断警告和工具链
`.note.GNU-stack` linker warning。默认构建只生成 ELF/MAP，没有 BIN/HEX。

## 11. 尚未解决的问题

- 需用当前 64 位 host `luavm` 重新打包所有开发 Cart。
- CrashRecord 受控 UDF 全链路未做；不能声称已实机验证。
- QFlash storage 的真实掉电/拔卡恢复未做，当前只有共享格式与 host 故障注入证据。
- audio task 无实际命令，栈高水位统计仍不可用。
- logger/completion queue 满的跨模块故障注入尚未形成统一端到端 target test。

## 12. 下一步建议

唯一推荐下一任务：用当前 `build/HostTest/bin/luavm` 重新打包 Foundation API 测试 Cart，
在 SD 上完成 storage commit/reload、正常应用 100 次启动退出及旧 Cart 迁移验证。
验收条件是新 Cart ENTRY 能加载、100 次正常启动退出 heap/owner/stale completion 回到
基线、storage 边界在 QFlash 重启后保持一致，并将测试 Cart 的生成命令纳入仓库文档。
