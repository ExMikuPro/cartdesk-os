# DMA2D Standalone Self-Test

`SizeDebug-DMA2D-SelfTest` 是仅用于目标板诊断的构建配置。它不使用 LVGL、Lua、JPEG、Cart 或 resource manager 参与 DMA2D 测试；GDB 只写 mailbox，实际 DMA2D 操作在 `Launcher_Task()` 的正常 app task 上下文执行。

当前只实现并允许执行 L1 / Test 1。其它 standalone 或 Resource Pipeline V2 integration 层级尚未实现，不能将本文件或该 preset 解释为其已通过真机验证。

## 构建与触发

```sh
cmake --preset SizeDebug-DMA2D-SelfTest
cmake --build --preset SizeDebug-DMA2D-SelfTest
arm-none-eabi-gdb -x tools/gdb/dma2d_selftest.gdb
```

脚本使用仓库根目录的 `CartDeck.cfg` 对应 OpenOCD 会话。GDB 写 `g_dma2d_test_command = 1` 请求 R2M fill。脚本打印结果、首个错误像素和 DMA2D 寄存器快照；HardFault、MemManage、BusFault、UsageFault 由已有 crash record 保存 PC、LR、SP、CFSR、HFSR、MMFAR、BFAR。

## L1 — R2M fill

测试从 `DMA_POOL` 正式分配一个 32-byte 对齐的独占 SDRAM 区域，不重置 DMA_POOL，因而不会覆盖 LTDC framebuffer、RESOURCE_ARENA、Lua heap 或其他 DMA_POOL owner。布局为：

```text
[32 × 0xA5A5A5A5 guard][800 × 480 ARGB8888][32 × 0x5A5A5A5A guard]
```

CPU 先把整个 framebuffer 初始化为 `0xFF112233`。DMA2D R2M 再将 `(137, 83, 200, 200)` 填为 `0xFFFF0000`，目标地址等价于：

```c
(uint8_t *)fb + ((83u * 800u + 137u) * sizeof(uint32_t))
```

测试完成后按像素比较整个 `800 × 480` buffer，检查两端 guard，并保存第一个错误的坐标、expected/actual、DWT CYCCNT 时间和以下寄存器：`CR`、`ISR`、`FGMAR`、`FGOR`、`BGMAR`、`BGOR`、`FGPFCCR`、`BGPFCCR`、`OMAR`、`OOR`、`OPFCCR`、`NLR`。

本测试显式令 `CR.LOM = 0`，所以 offset 的单位是 pixel：`OOR = 800 - 200 = 600`；`NLR.PL = 200`、`NLR.NL = 200`。这符合 STM32H743 [RM0433 Rev.8 DMA2D 章节](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)：LOM=0 时 OOR/FGOR/BGOR 的 line offset 以像素表示，LOM=1 时以字节表示。

## 结果判定

`g_dma2d_test_state`：`2` 为 PASS、`3` 为 FAIL。PASS 必须同时满足：DMA2D 无 TEIF/CEIF、guard 完整，且 384000 个像素全部符合 expected。屏幕显示不作为判定依据。

DMA_POOL 位于 SDRAM `0xD1465000..0xD1865000`，由 `Core/Memory/xhgc_memory_layout.c` 定义。启动期 MPU 将 SDRAM 地址窗口配置为 non-cacheable，故 L1 不用于验证 cache coherency；它仍通过 `xhgc_dcache_*` 按 32-byte cache-line 维护范围，固定 CPU/DMA 的测试边界。DCache 矩阵属于后续层级。

## 真机检查结果

2026-09-28，使用 `CartDeck.cfg`、OpenOCD 0.12.0、ST-Link 和
`SizeDebug-DMA2D-SelfTest` 在 STM32H743 上执行 L1：

| Test | Result | Register / measurement evidence |
| --- | --- | --- |
| R2M fill | PASS | `state=2`、`pass=1`、`fail=0`、fault=0 |
| Full framebuffer compare | PASS | 384000 pixels checked; no first-error coordinate |
| Guard regions | PASS | both guard regions unchanged |
| LOM / OOR / NLR | PASS | `LOM=0`、`OOR=600`、`NLR=0x00C800C8` |
| R2M timing | Observed | 582814 cycles / 1214 µs |

Snapshot: `CR=0x00030000`、`ISR=0x00000002`、`OMAR=0xD14A6024`、`OPFCCR=0x00000000`。因此，当前证据表明 DMA2D R2M 二维填充和任意非零的本例 `x=137,y=83` 地址计算正确；本结果不能外推为 FGOR、BGOR、alpha、JPEG 或 cache matrix 已验证。
