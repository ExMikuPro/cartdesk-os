# DMA2D Standalone Self-Test

`SizeDebug-DMA2D-SelfTest` 是仅用于目标板诊断的构建配置。它不使用 LVGL、Lua、JPEG、Cart 或 resource manager 参与 DMA2D 测试；GDB 只写 mailbox，实际 DMA2D 操作在 `Launcher_Task()` 的正常 app task 上下文执行。

当前实现并允许执行 L1 至 L4。其它 standalone 或 Resource Pipeline V2 integration 层级尚未实现，不能将本文件或该 preset 解释为其已通过真机验证。

## 构建与触发

```sh
cmake --preset SizeDebug-DMA2D-SelfTest
cmake --build --preset SizeDebug-DMA2D-SelfTest
arm-none-eabi-gdb -x tools/gdb/dma2d_selftest.gdb
```

脚本使用仓库根目录的 `CartDeck.cfg` 对应 OpenOCD 会话。GDB 写 `g_dma2d_test_command = 1` 请求 R2M fill，写 `2` 请求 M2M tight 坐标矩阵，写 `3` 请求 M2M strided-source 坐标矩阵，写 `4` 请求 RGB565 M2M PFC。脚本默认运行 command 4；它打印结果、首个错误像素、operation/case、source/destination address 与 stride，以及 DMA2D 寄存器快照。HardFault、MemManage、BusFault、UsageFault 由已有 crash record 保存 PC、LR、SP、CFSR、HFSR、MMFAR、BFAR。

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

## L2 — M2M tight source

L2 以紧凑的 `200 × 200 ARGB8888` source（`FGOR=0`）写到 800 pixel stride 的 framebuffer。source 每个像素编码自己的 `(x,y)`，因此可识别错误的 source 行步进或像素地址。destination 逐一测试：

```text
x = 0, 1, 7, 31, 32, 137, 599
y = 0, 1, 37, 279
```

每一组都重新初始化 destination、执行 DMA2D、检查 guard，并比较完整 framebuffer。L2 同样显式使用 `LOM=0` 和 `OOR=600`。

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
| M2M tight coordinate matrix | PASS | 28 groups; `state=2`、`pass=1`、`fail=0` |

Snapshot: `CR=0x00030000`、`ISR=0x00000002`、`OMAR=0xD14A6024`、`OPFCCR=0x00000000`。因此，当前证据表明 DMA2D R2M 二维填充和任意非零的本例 `x=137,y=83` 地址计算正确；本结果不能外推为 FGOR、BGOR、alpha、JPEG 或 cache matrix 已验证。

L2 最后一组的 snapshot 为 `LOM=0`、`FGOR=0`、`OOR=600`、`NLR=0x00C800C8`，计时为 1229425 cycles / 2561 µs。它确认 tight source 与这些非对齐 destination 坐标的二维寻址正确；仍不能外推为 strided source、BGOR、alpha、JPEG 或 cache matrix 已验证。

## L3 — M2M strided source / FGOR

L3 使用 `256 × 256 ARGB8888` physical source，全部像素按物理坐标编码：

```c
0xFF000000u | ((x & 0xFFu) << 16) | ((y & 0xFFu) << 8) | ((x ^ y) & 0xFFu)
```

每次选择 `200 × 200` region。测试矩阵覆盖 source `sx = 0, 1, 7, 17, 31, 32, 55`、`sy = 0, 1, 23, 37`，以及 destination `dx = 599, 137, 32, 31, 7, 1, 0`、`dy = 279, 37, 1, 0` 的 28 个组合；另加 `(sx, sy, dx, dy) = (56, 56, 0, 0)`，使 source region 精确触及 physical source 的右、下边界。29 组共同覆盖 source/destination 的对齐与非对齐位置及各边界。

`LOM=0` 时，L3 使用 `FGOR = 256 - 200 = 56`、`OOR = 800 - 200 = 600`、`NLR=0x00C800C8`。每个组合重新初始化 destination，传输后检查两端 guard 并比较完整 `800 × 480` framebuffer：region 内须等于选中的 physical source region，region 外须保持 background。

### L3 真机检查结果

2026-09-28（Asia/Tokyo），使用 CartDeck / STM32H743、ST-Link、OpenOCD 0.12.0 和
`SizeDebug-DMA2D-SelfTest` 执行。Phase 1 基线为 `ea108cf` 与 `cb5dbcd`；本次烧录的
ELF SHA-256 为 `a15b8d6e29d547cf842313b0355fa8b6001a718417835756b9565abd163617af`。

| Test | Result | LTDC | DCache | Key registers | Fault |
| --- | --- | --- | --- | --- | --- |
| M2M strided source / FGOR | PASS, 29/29 cases | isolated | SDRAM non-cacheable | `LOM=0`, `FGOR=56`, `OOR=600`, `NLR=0x00C800C8` | 0 |
| Full framebuffer compare | PASS | isolated | SDRAM non-cacheable | selected region only; outside unchanged | 0 |
| Guard regions | PASS | isolated | SDRAM non-cacheable | both guards unchanged | 0 |
| Source right/bottom boundary | PASS | isolated | SDRAM non-cacheable | case 29: `(56,56) -> (0,0)` | 0 |

最终 case 的 mailbox snapshot：`state=2`、`pass=1`、`fail=0`、`operation=3`、`case=29`、
`source=0xD14730E0`、`source_stride=256`、`destination=0xD14A5080`、`destination_stride=800`、
`cycles=1678120`、`time_us=3496`、`CR=0x00000000`、`ISR=0x00000002`、`FGMAR=0xD14730E0`、
`FGOR=56`、`OMAR=0xD14A5080`、`OOR=600`、`OPFCCR=0x00000000`、`NLR=0x00C800C8`。此 timing
仅为最后一个 200×200 transfer 的观测值，不构成 Phase 8 性能结论。

## L4 — M2M PFC RGB565 → ARGB8888

L4 不依赖 JPEG、LVGL、Resource V2 或现有 DMA2D helper。它以独立 RGB565 source 测试红、绿、蓝、白、黑、灰、黄、青、品红，以及 `0x1234`、`0x2A95`、`0x6B4D` 等非极值位型；CPU reference 显式将 `R5/G6/B5` 通过 bit replication 展开为 `R8/G8/B8`，并固定 alpha 为 `255`。

先执行 tight `200 × 200` source（`FGOR=0`），再执行 physical `256 × 256` strided source 的 `(sx, sy)=(17, 23)` region（`FGOR=56`）。两者均输出到 `(137,83)` 的 ARGB8888 framebuffer，使用 `OOR=600`，并逐像素比较完整 framebuffer 与两端 guard。

### L4 真机检查结果

2026-09-28（Asia/Tokyo），CartDeck / STM32H743、ST-Link、OpenOCD 0.12.0；本次烧录
ELF SHA-256 为 `f406d02f9f19aa9a93c6931d98586139c51641af695dac0038be0619ec29e197`。

| Test | Result | LTDC | DCache | Key registers | Fault |
| --- | --- | --- | --- | --- | --- |
| RGB565 PFC tight | PASS | isolated | SDRAM non-cacheable | `FGOR=0`, `OOR=600` | 0 |
| RGB565 PFC strided | PASS | isolated | SDRAM non-cacheable | `LOM=0`, `FGOR=56`, `OOR=600`, `NLR=0x00C800C8` | 0 |
| CPU reference / guard / full compare | PASS | isolated | SDRAM non-cacheable | ARGB8888 bit-replication output | 0 |

strided case mailbox：`state=2`、`pass=1`、`fail=0`、`operation=4`、`case=2`、fault=0；
`FGMAR=0xD147B6A2`、`FGOR=56`、`FGPFCCR=0x00000002 (RGB565)`、`OMAR=0xD14D98A4`、
`OOR=600`、`OPFCCR=0x00000000 (ARGB8888)`、`NLR=0x00C800C8`、`cycles=1092332`、`time_us=2275`。
