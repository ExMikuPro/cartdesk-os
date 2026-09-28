# Resource Pipeline V2 目标板验证报告

验证日期：2026-09-24  
目标：STM32H743、SizeDebug、OpenOCD/GDB、DWT CYCCNT、RuntimeStats  
分支：`codex/resource-pipeline-v2`

## 结论

**NOT READY**

纯 JPEG 的 XIMG v2、STM32H743 JPEG peripheral、DMA2D YCbCr 转换和 LVGL
显示链路均已在目标板通过；但是当前异步 blob staging 使用 FreeRTOS heap：

```text
pvPortMalloc(record->meta->size)
```

目标板稳定空闲值只有 22,672 bytes。因此 200×200 JPEG+A8（43,380 / 44,556
bytes）与 legacy BGRA8888（160,000 bytes）在提交 SD 读取前就会进入
`RES_FAILED`。这会直接阻断本轮要求的 JPEG+A8 与 legacy 兼容性，不能合入。

此外，800×480 JPEG 的 completion 在 app task 中连续占用 83.006–160.602 ms，
约为 60 Hz 的 4.98–9.64 帧预算，说明 polling JPEG/DMA2D 会形成明显 frame spike。

## 测试 Cart

目标板上的 `cart.bin` 由当前 `xhgc-pack` 分支实际生成并通过 `verify-cart`：

- cart size：1,261,568 bytes
- SHA-256：`a262c53fe7fa88aec4fb16d025e2775518a7ee1a37dd2c47e5b03dab4809a0df`
- XHGCIDX2 v1、32-byte entry
- JPEG quality 90
- 200×200 与 800×480
- JPEG 4:4:4 / 4:2:0
- JPEG+A8 4:4:4 / 4:2:0
- legacy BGRA8888

## 真机时延

时延来自 SizeDebug 中只记录固定 counter、duration 和 byte count 的临时 DWT
instrumentation；没有在 hot path 打印。`SD read` 包含 blob read 与 CRC 校验，
`App blocked total` 是 app task 处理该 resource completion 的连续时间。

| Resource | Resolution | Stored bytes | Runtime bytes | SD read | JPEG decode | DMA2D | A8 | App blocked total | Total latency |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| JPEG 4:4:4 | 200×200 | 2,398 | 160,000 | 4.005 ms | 17.571 ms | 0.417 ms | — | 18.151 ms | 40.679 ms |
| JPEG 4:2:0 | 200×200 | 2,259 | 160,000 | 2.779 ms | 9.527 ms | 0.415 ms | — | 10.107 ms | 21.868 ms |
| JPEG+A8 4:4:4 | 200×200 | 44,556 | 160,000 | 未开始 | 未开始 | 未开始 | 未开始 | 分配失败 | 分配失败 |
| JPEG+A8 4:2:0 | 200×200 | 43,380 | 160,000 | 未开始 | 未开始 | 未开始 | 未开始 | 分配失败 | 分配失败 |
| legacy BGRA8888 | 200×200 | 160,000 | 160,000 | 未开始 | — | — | — | 分配失败 | 分配失败 |
| JPEG 4:4:4 | 800×480 | 12,628 | 1,536,000 | 344.005 ms | 155.321 ms | 3.861 ms | — | 160.602 ms | 510.768 ms |
| JPEG 4:2:0 | 800×480 | 7,401 | 1,536,000 | 9.588 ms | 77.729 ms | 3.858 ms | — | 83.006 ms | 532.796 ms |
| JPEG+A8 4:4:4 | 800×480 | 405,688 | 1,536,000 | 未开始 | 未开始 | 未开始 | 未开始 | 分配失败 | 分配失败 |
| JPEG+A8 4:2:0 | 800×480 | 398,300 | 1,536,000 | 未开始 | 未开始 | 未开始 | 未开始 | 分配失败 | 分配失败 |

800×480 4:4:4 的首个 SD read 出现 344.005 ms 冷读/卡延迟样本；这发生在 IO
task，不阻塞 app task。4:2:0 样本为 9.588 ms。两者的 app task spike 则由
completion 内的 polling decode/conversion 直接造成。

## 硬件链路证据

目标板计数器记录 4 次 HAL JPEG 调用与 4 次 DMA2D 调用，四个纯 JPEG 资源均为
一次读、一次 decode、一次 conversion：

```text
resource completion
-> CartJpegDecoder_DecodeXimgV2
-> HAL_JPEG_Decode / HAL_JPEG_GetInfo
-> HAL_DMA2D_Start / HAL_DMA2D_PollForTransfer
-> RESOURCE_ARENA
-> RES_READY
```

没有进入 TJpgDec、libjpeg-turbo、Cortex-M7 software JPEG 或 host fallback。

4:4:4 与 4:2:0 的 200×200、800×480 均 READY，无 JPEG HAL error、HardFault
或尺寸错误。200×200 色块在 READY 内存中的 BGRA 抽样为：

| 色块 | BGRA bytes |
| --- | --- |
| red | `00 00 FE FF` |
| green | `01 FF 00 FF` |
| blue | `FE 00 00 FF` |
| white | `FF FF FF FF` |
| black | `00 00 00 FF` |

活动 framebuffer 同样显示红、绿、蓝、白、黑色带，未发现 R/B swap。

## 生命周期与 dedup

- 同一 200×200 JPEG 4:4:4 同时创建 4 个 handle：`acquire_count=4`、
  `io_count=1`、`decode_count=1`。
- READY 后 framebuffer 刷新期间硬件 JPEG、DMA2D 与 SD blob counter 不再增长。
- `crop` / `flip` 源码路径是 READY 后的 view-buffer CPU copy，不会重新 JPEG
  decode；由于 legacy BGRA staging 失败，本轮无法完成 legacy view 的真机显示验收。
- LOADING 后立即 EXIT 自动循环 50 次：50 completed、0 failures、无 fault；结束时
  Lua runtime IDLE、SD pending 0、RESOURCE_ARENA used/alive 0、refcount anomaly 0、
  FreeRTOS heap 回到 22,672 bytes。下一次 cart 启动正常。

## Frame budget

60 Hz 单帧预算为 16.67 ms：

| Resource | Completion | Frame budgets |
| --- | ---: | ---: |
| JPEG 4:4:4 200×200 | 18.151 ms | 1.09 |
| JPEG 4:2:0 200×200 | 10.107 ms | 0.61 |
| JPEG 4:4:4 800×480 | 160.602 ms | 9.64 |
| JPEG 4:2:0 800×480 | 83.006 ms | 4.98 |

因此 SD read 不阻塞 app，但 JPEG decode 与 DMA2D polling 均阻塞 app。大图的主要
占用来自 JPEG decode（77.729–155.321 ms），DMA2D 为 3.858–3.861 ms。

## 连续资源与内存限制

首次同时请求资源时，多个小 JPEG 能按 completion budget 逐个完成，但第三个较大
staging 分配后即受 FreeRTOS heap 限制。当前实现因此无法可靠完成 4/8/16 个不同
资源的 burst 验收；这是实际失败，不是“未测”。completion budget 没有改变单个
大图 decode 在 app task 中连续占用 83–161 ms 的事实。

## Phase V2.1 设计建议（未实施）

在修复 staging buffer 所有权后，建议优先评估方案 A：让现有 IO worker 串行执行
SD read、JPEG decode、DMA2D，再向 app task 投递 READY completion。它不增加第五个
task，能直接移除 app task 的 5–10 帧 decode spike；代价是 IO 队列在大图 decode
期间被串行占用，必须用 generation/cancellation 保护预留的 RESOURCE_ARENA 输出。

- 方案 A：复杂度最低；IO 与 decode 串行；需要明确 RESOURCE_ARENA 输出预留、取消
  与 stale completion 回收。
- 方案 B：独立 decode worker 可并行 SD 与 decode，吞吐更好；但新增 task、队列、
  优先级、scratch/output ownership 和取消状态，复杂度显著更高。
- 方案 C：JPEG interrupt-driven state machine 响应最佳；HAL 状态、DMA buffer lifetime、
  cancellation 与错误恢复最复杂，不适合作为当前阻断修复的第一步。

本轮没有实施 V2.1。

## CubeMX Generate Code

Headless STM32CubeMX 6.18.1 在临时完整副本中仍停在数据库
`DB.6.0.170` 的 `Begin LoadConfig()`；现象发生在项目生成前，判断为 CubeMX
workspace/database 环境问题，不是 `.ioc` 解析后改写导致的问题。

按计划尝试了 GUI，但 macOS 未授予自动化所需的 Accessibility / Screen Recording
权限，无法执行 Generate Code。因此以下项目仍未关闭：

- Generate Code 后 JPEG peripheral / `jpeg.c/.h` 保留
- custom decoder 防覆盖 diff
- Generate Code 后 Debug build
- Generate Code 后 Debug-USB-SD-MSC build

当前未生成源码树本身的 Debug 与 Debug-USB-SD-MSC 构建均已通过。专用 MSC 固件能
进入 active 且主机识别 63.9 GB `SDCARD`，但 macOS 对 LUN 读请求无响应、无法挂载；
该现象独立于 Resource V2 JPEG 路径。

## 最终问题回答

1. JPEG 4:4:4 真机：通过（200×200、800×480）。
2. JPEG 4:2:0 真机：通过（200×200、800×480）。
3. JPEG+A8 alpha：不通过；200×200 在 staging 分配前失败，未进入 decode/A8。
4. READY 后滚动：计数器不再发生 SD read/decode/A8；已确认纯 JPEG。
5. polling JPEG 最长连续占用：155.321 ms。
6. polling DMA2D 最长连续占用：3.861 ms。
7. 800×480：产生 83.006–160.602 ms completion spike，可见风险明确。
8. 是否迁出 app task：需要；先修 staging，再以 Phase V2.1 方案 A 为首选评估。
9. LOADING EXIT 50 次：通过，50/50、0 failures、无 fault/泄漏。
10. legacy BGRA 真机：不通过；160,000-byte staging 无法从 22,672-byte heap 分配。
11. CubeMX Generate Code：未成功；headless 数据库卡住，GUI 权限未放行。
12. Generate 后双构建：未执行；未生成源码树的两个 preset 均通过。

