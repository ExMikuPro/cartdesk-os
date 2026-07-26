# 第三阶段性能优化

## 1. 测试场景

测试固件为 `SizeDebug`，MCU 主频 480 MHz，通过 OpenOCD/GDB 读取
DWT、RuntimeStats、SD 读取轨迹和资源占用。

实机使用 SD 卡上的 `0:/cart.bin`：

- 标题：Hatsune Miku
- 入口：`app/main.lua`
- Lua 字节码：Lua 5.4
- 一个按钮、一个 200 × 200 BGRA8888 图片对象
- 图片像素：160000 B
- INDEX 中共 3 个资源

通过仅在 `PERF_MONITOR_ENABLE` 构建中存在的 GDB mailbox，在 LVGL 主任务
上下文执行启动、运行、退出。最终完成连续 20 次启动/退出，失败 0 次。

当前 cart 没有持续动画、多张图片或复杂 `update()`，因此本轮数据只能作为
资源加载和生命周期基线，不能代表完整游戏压力。此前启动的 10 分钟测试因重新
打包 cart 被主动终止；物理触摸、拔卡恢复和 USB 枚举没有在本轮自动验证。

## 2. 启动耗时分解

以下数据来自 480 MHz DWT；未能单独取得稳定数据的阶段标为“未单独测得”。

| 阶段 | 调用次数 | 平均 | 最大 | 总耗时 | 占启动比例 |
| --- | ---: | ---: | ---: | ---: | ---: |
| QFLASH mount | 1 | 约 1 μs | 约 1 μs | 约 1 μs | <0.01% |
| QFLASH 字库校验 | 0 | — | — | — | — |
| Launcher 预览图读取，优化前 | 1 | 96.408 ms | 96.408 ms | 96.408 ms | 12.1% |
| Launcher 预览图读取，连续读取后 | 1 | 32.765 ms | 32.765 ms | 32.765 ms | 4.5% |
| Launcher 对象创建，优化前 | 1 | 180.602 ms | 180.602 ms | 180.602 ms | 22.7% |
| Launcher 对象创建，连续读取后 | 1 | 116.962 ms | 116.962 ms | 116.962 ms | 14.7% |
| Launcher 基础对象创建，延迟预览后 | 1 | 84.126 ms | 84.126 ms | 84.126 ms | 11.5% |
| 首次 `lv_timer_handler`，优化前 | 1 | 205.550 ms | 205.550 ms | 205.550 ms | 25.8% |
| 首次 `lv_timer_handler`，延迟预览后 | 1 | 170.529 ms | 170.529 ms | 170.529 ms | 23.4% |
| 首次 page flip / VBlank 等待 | 1 | 2–17 ms | 17 ms | 2–17 ms | 随 VBlank 相位变化 |
| 首屏可见，优化前 | 1 | 797 ms | 797 ms | 797 ms | 100% |
| 首屏可见，优化后 | 1 | 730 ms | 730 ms | 730 ms | 100% |

QFLASH 字库在 Header/版本检查处回退，完整校验统计点没有执行。这不构成当前
启动耗时瓶颈，但字库包版本或写入内容仍需单独检查。

## 3. 压力场景结果

| 指标 | 基线 | 最终 | 变化 |
| --- | ---: | ---: | ---: |
| 首屏可见时间 | 797 ms | 730 ms | -67 ms，-8.4% |
| 启动阶段最大慢帧 | 约 877.9 ms | 约 843–865 ms | 有运行间波动 |
| Launcher 预览图读取 | 96.408 ms | 32.765 ms | -66.0% |
| 单次应用进入 Lua 路径 | 189.0 ms | 182.2 ms | -6.8 ms，-3.6% |
| 单次应用进入 SD 底层请求 | 114 | 38 | -66.7% |
| 单次应用进入 SD 总耗时 | 103.8 ms | 104.1 ms | 基本不变 |
| Lua update 最大耗时 | 166.7 ms | 161.2 ms | 启动调用主导 |
| flush wait 最大耗时 | 147.2 ms | 17.0 ms（最终运行） | 启动/VBlank 相位相关 |
| 主任务栈峰值 | 约 7.5 KiB | 约 7.5 KiB | 无持续增长 |
| Lua heap 峰值 | 约 32 KiB | 23.5 KiB | cart 运行实测 |
| FreeRTOS heap 最小剩余 | 56832 B | 56832 B | 无变化 |
| 资源 arena 峰值 | 160000 B | 160000 B | 无变化 |
| SizeDebug FLASH | 约 697352 B | 697304 B | -48 B |
| SizeDebug AXI RAM | 368832 B | 364736 B | -4096 B |
| SizeDebug DTCMRAM | 54880 B | 54880 B | 无变化 |
| SizeDebug D2 RAM | 4128 B | 4128 B | 无变化 |
| 静态 SDRAM | 1966848 B | 1966848 B | 无变化 |

最终四种构建的 `arm-none-eabi-size`：

| 配置 | text | data | bss |
| --- | ---: | ---: | ---: |
| Debug | 771180 B | 1060 B | 2389560 B |
| SizeDebug | 696236 B | 1060 B | 2389528 B |
| Release | 694924 B | 1052 B | 2386248 B |
| Release-MinSize | 663836 B | 960 B | 2386176 B |

## 4. 每项优化前后对比

### 保留：Launcher 预览图连续读取

| 项目 | 内容 |
| --- | --- |
| 瓶颈 | 200 行预览图逐行读取，产生大量 FatFs/SD 请求 |
| 证据 | 预览读取 96.408 ms，SD 请求 319 次 |
| 修改 | 一次读取连续的 160000 B ARGB8888 预览 |
| Flash 变化 | -24 B（该次修改测量） |
| RAM 变化 | -800 B 行缓冲 |
| 平均耗时变化 | 96.408 ms → 32.765 ms |
| 最大耗时变化 | 同平均值 |
| 慢帧变化 | Launcher 对象创建 180.602 ms → 116.962 ms |
| 风险 | 目标缓冲必须位于 SDMMC 可访问且对齐的 SDRAM |
| 结论 | 保留 |

### 保留：首屏后加载 Launcher 预览图

| 项目 | 内容 |
| --- | --- |
| 瓶颈 | 预览图读取阻塞首个 `lv_timer_handler` 前的对象创建 |
| 证据 | 首屏 797 ms，首次 LVGL handler 205.550 ms |
| 修改 | 先创建基础 Launcher，随后在同一 LVGL 任务加载预览 |
| Flash 变化 | 包含在最终构建数据 |
| RAM 变化 | 无新增无界缓存 |
| 平均耗时变化 | 首屏 797 ms → 730 ms |
| 最大耗时变化 | 最大慢帧通常下降约 4%，有启动波动 |
| 慢帧变化 | 基础对象创建降至 84.126 ms |
| 风险 | 预览短暂晚于基础界面出现；失败时下次重建重试 |
| 结论 | 保留 |

### 保留：Cart 图片直接读入 SDRAM

| 项目 | 内容 |
| --- | --- |
| 瓶颈 | 160000 B 图片被拆成 4 KiB 块，先读 bounce buffer 再 CPU `memcpy` |
| 证据 | 单次进入应用 189.0 ms；114 次底层 SD 请求 |
| 修改 | SDMMC IDMA 直接读取到最终 RESOURCE_ARENA 缓冲 |
| Flash 变化 | -48 B |
| RAM 变化 | -4096 B |
| 平均耗时变化 | 189.0 ms → 182.2 ms，-3.6% |
| 最大耗时变化 | Lua 启动峰值 166.7 ms → 161.2 ms |
| 慢帧变化 | 小幅下降，SD 传输仍是主导 |
| 风险 | 依赖 SD 驱动对非对齐头尾使用固定 scratch buffer |
| 结论 | 保留；收益超过 3%，并减少 RAM 和复制 |

### 回退：扫描预览图 Alpha 并改用 XRGB8888

| 项目 | 内容 |
| --- | --- |
| 瓶颈 | 怀疑不透明图片仍触发 ARGB 混合 |
| 证据 | GDB 检查 40000/40000 像素 Alpha 均为 255 |
| 修改 | 运行时扫描 Alpha 后选择 XRGB8888 |
| 平均耗时变化 | Launcher 预览加载约 35.0 ms → 41.5 ms |
| 最大耗时变化 | 慢帧约 843.3 ms → 854.3 ms |
| 风险 | 增加每次加载扫描和格式分支 |
| 结论 | 收益为负，已完整回退 |

### 未采用：在 Cart 图片路径增加 MDMA

SDMMC IDMA 已能直接写最终 SDRAM。实测优化后 SD 传输仍约 104 ms，而移除
CPU 中间复制只减少约 6.8 ms。再增加 MDMA 会多一次搬运、Cache 维护和同步
等待，无法缩短 SD 卡传输，因此没有实施。

## 5. 正确性和回归测试

- 新版 cart Header、INDEX、ENTRY 和 DATA 均能通过固件校验。
- 修正 CRC 外设输入按字节反转和输出反转后，Lua 能进入 `RUNNING`。
- 连续启动/退出 20 次：20 次完成，失败 0 次。
- 结束时 Lua 状态：`IDLE`，错误：`NONE`。
- Lua heap 峰值：23552 B。
- 资源 arena 峰值：160000 B，资源引用异常：0。
- FreeRTOS heap 剩余：56832 B。
- 主任务 stack high-water 剩余 25268 B，约使用 7.5 KiB。
- 未观察到 HardFault。
- Debug、SizeDebug、Release、Release-MinSize 均构建成功。
- `ctest` 没有发现已注册测试。
- Release 存在 Launcher 文件大小格式化的既有截断警告，本轮未扩大修改范围。
- 物理触摸、USB 枚举、拔卡恢复和完整 10 分钟运行仍未完成。

## 6. 剩余性能问题

| 优先级 | 问题 | 证据 | 建议 |
| --- | --- | --- | --- |
| P1 | SD 图片同步读取阻塞应用首帧 | 160000 B 图片的 SD 传输约 104 ms | 基础应用页先显示，再分阶段加载首屏图片 |
| P1 | LVGL 图片任务没有真正使用 DMA2D | DMA2D RuntimeStats 调用数为 0；DMA2D draw unit 对 image task 返回 0 | 在不修改第三方库的前提下评估受支持的 fill/blend/convert 路径 |
| P1 | 真实压力 cart 覆盖不足 | 当前仅 1 张图片、1 个按钮、空 `update()` | 增加动画、多图、触摸和懒加载测试 cart |
| P2 | QFLASH 字库 mount 回退 | Header/版本检查失败，完整校验未执行 | 核对实际写入包版本及 QFLASH 内容 |
| P2 | SD INDEX 存在多个小读取 | 单次应用进入仍有 38 次底层请求 | 缓存有界 Header/INDEX，合并索引字符串读取 |
| P3 | 固定 5 ms 调度 | 空闲工作时间极低 | 动态调度收益不足前保持现状 |

## 7. 文件清单

开始前已存在但未修改：

- `PERFORMANCE_AUDIT.md`（未跟踪）

本阶段修改：

- `CMakeLists.txt`
- `Core/APPS/LVGL/font/qflash_font.c`
- `Core/APPS/LVGL/port/lv_port_disp.c`
- `Core/Cart/cart_bin.c`
- `Core/Cart/cart_index.c`
- `Core/Debug/perf_monitor.c`
- `Core/Debug/perf_monitor.h`
- `Core/Screen/Page/ui_screen_launcher.c`
- `Core/Src/crc.c`
- `Core/Src/main.c`
- `FATFS/Target/sd_diskio.c`
- `cartdesk-os.ioc`

本阶段新增：

- `PERFORMANCE_OPTIMIZATION_PHASE3.md`

检查但未修改：

- `Core/APPS/LVGL/src/draw/dma2d/lv_draw_dma2d.c`
- `Core/LuaPort/lua_cart_resource_cache.c`
- `Core/LuaPort/resource_manager.c`
- `Core/Cart/xhgc_cart.c`
- `Core/Src/mdma.c`
- `PERFORMANCE_AUDIT.md`
