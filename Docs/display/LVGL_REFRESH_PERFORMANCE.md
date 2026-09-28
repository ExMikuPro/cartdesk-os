# LVGL 9.6 Refresh Performance

## 结论

### CORRECTNESS STABLE, PERFORMANCE BOTTLENECK IDENTIFIED

第一阶段已从 LVGL normal flip 移除旧 `disp_wait_for_vsync()` busy-wait。production
presentation 仍只经过：

```text
HAL_LTDC_SetAddress_NoReload
-> HAL_LTDC_Reload(LTDC_RELOAD_VERTICAL_BLANKING)
-> HAL_LTDC_ReloadEventCallback
-> generation match + semaphore
-> disp_flush_wait
```

相同 `+240 px / 12 steps / 20 px per rendered frame` 目标板测试中，ARGB pure render
平均从旧混合窗口的 91.48 ms 降到 83.37 ms，XRGB 从 83.00 ms 降到 75.61 ms。
两者仍远高于 33 ms；当前第一性能瓶颈是 `LV_EVENT_RENDER_START` 到
`LV_EVENT_RENDER_READY` 的 pure render，而不是 presentation wait 或 DIRECT steady-state
buffer sync。

## Baseline

修改前 deterministic scroll 数据：

| Metric | ARGB | XRGB |
|---|---:|---:|
| render min | 84.92 ms | 53.05 ms |
| render avg | 91.48 ms | 83.00 ms |
| render max | 101.91 ms | 91.75 ms |

旧窗口包含 `disp_wait_for_vsync()`，不能解释为纯 CPU/DMA2D render。

## Busy-wait removal

`Core/APPS/LVGL/port/lv_port_disp.c` 的 normal `disp_flush()` 不再调用
`disp_wait_for_vsync()`。公共 `disp_wait_vsync()`、`g_vsync_flag` 与 LTDC LineEvent 保留，
供 legacy API 和 VBlank 计数继续使用。

真正的 `RuntimeStats` flush-wait 计时已移到 `disp_flush_wait()`，因此现在测量的是
ReloadEvent generation/semaphore 等待，不再是提交前的 LineEvent busy-wait。

## Timing decomposition

`Debug-LTDC-Sync-Trace` 使用 DWT `CYCCNT`。事件定义如下：

| Metric | Begin | End | 含义 |
|---|---|---|---|
| `T_refresh` | `LV_EVENT_REFR_START` | `LV_EVENT_RENDER_READY` | layout、DIRECT sync、render 和 flush submit |
| `T_render` | `LV_EVENT_RENDER_START` | `LV_EVENT_RENDER_READY` | LVGL draw；不含旧 VSync busy-wait |
| `T_buffer_sync` | `refr_sync_areas()` copy 前 | sync list clear 后 | internal DIRECT framebuffer copy |
| `T_flush_submit` | final `disp_flush()` enter | VBR request | NoReload 与 VBR submit |
| `T_flush_wait` | `disp_flush_wait()` enter | callback return 前 | LVGL owner task 的 presentation wait |
| `T_reload_wait` | VBR request | matching ReloadEvent | hardware presentation latency |
| `T_total_frame` | refresh begin | 该 refresh 的 matching ReloadEvent | refresh-to-present latency |
| frame interval | consecutive ReloadEvent | consecutive ReloadEvent | frame pacing / jitter |

`Core/APPS/LVGL/src/core/lv_refr.c` 只增加 trace-only sync begin/end、area count 与 pixel
count；没有改变 `refr_sync_areas()` 的 copy 路径或算法。

## Target results

目标板环境：STM32H743、480 MHz DWT、panel period 16.984 ms、
`Debug-LTDC-Sync-Trace`。采样 session：
`build/Debug-LTDC-Sync-Trace/captures/20260928_232414`。

| Metric | Before ARGB | After ARGB | Before XRGB | After XRGB |
|---|---:|---:|---:|---:|
| render min | 84.92 ms | 80.07 ms | 53.05 ms | 72.96 ms |
| render avg | 91.48 ms | 83.37 ms | 83.00 ms | 75.61 ms |
| render max | 101.91 ms | 85.75 ms | 91.75 ms | 77.87 ms |
| flush wait avg | 未采样 | 5.21 ms | 未采样 | 3.54 ms |
| reload latency avg | 未采样 | 9.00 ms | 未采样 | 8.32 ms |
| frame interval avg | 未采样 | 93.99 ms | 未采样 | 83.85 ms |
| frame interval p95 | 未采样 | 101.91 ms | 未采样 | 84.92 ms |
| frame interval p99 | 未采样 | 101.91 ms | 未采样 | 84.92 ms |
| frame interval max | 未采样 | 101.91 ms | 未采样 | 84.92 ms |

补充分解：

| Metric | ARGB avg | XRGB avg |
|---|---:|---:|
| `T_refresh` | 90.38 ms | 80.94 ms |
| `T_render` | 83.37 ms | 75.61 ms |
| `T_buffer_sync` | 1.77 ms | 1.77 ms |
| `T_flush_submit` | 0.010 ms | 0.010 ms |
| `T_flush_wait` | 5.21 ms | 3.54 ms |
| `T_reload_wait` | 9.00 ms | 8.32 ms |
| `T_total_frame` | 99.38 ms | 89.26 ms |

`T_buffer_sync` 平均值被第一帧的 visual-mode transition 放大：第一帧同步 9,120
pixels，约 21.21 ms；后续 11 个 steady-state scroll frames 的 sync pixel count 均为 0，
sync bookkeeping 约 0.006--0.007 ms。因此本轮数据不支持把 DIRECT sync 作为 steady-state
首要瓶颈。

## Dirty-area statistics

两种格式每帧均有 2 个 flush area，共 280,064 pixels：

```text
screen pixels = 800 * 480 = 384,000
dirty ratio   = 280,064 / 384,000 = 72.9333%
```

12 帧的 avg / p95 / max dirty ratio 都是 72.9333%。这说明 Launcher 横向移动确实造成大面积
redraw；本轮未发现 CartDesk 额外扩大 invalidation 的证据。

## DMA2D statistics

| Counter | ARGB | XRGB |
|---|---:|---:|
| image tasks | 46 | 46 |
| M2M_BLEND | 46 | 0 |
| M2M_PFC | 0 | 46 |
| fill | 65 | 65 |
| software image | 60 | 60 |

ARGB 与 XRGB pure render 平均相差 7.76 ms，ARGB 约慢 10.3%。格式仍是 P2 性能因素，
不足以解释 75--83 ms 的主要 render cost。

## Frame interval distribution

ARGB 的 11 个 presentation intervals 中，5 个约为 5 panel frames，6 个约为 6 panel
frames；平均 93.99 ms，standard deviation 8.70 ms。XRGB 中 10 个约为 5 panel frames，
最后一个为 4 frames；平均 83.85 ms，standard deviation 3.38 ms。

主机工具生成：

- `frame_interval.csv`
- `frame_interval.png`
- `timing_breakdown.csv`
- `timing_summary.json`

PNG 优先使用 matplotlib；环境没有 matplotlib 时使用 Python 标准库生成同一 histogram，
不依赖 seaborn。

## Correctness checks

- ARGB：reload request/event/complete = 12/12/12。
- XRGB：reload request/event/complete = 12/12/12。
- 两者 reload timeout、ownership violation、LTDC FIFO underrun、transfer error、other error 均为 0。
- trace 中 normal flip 没有 VSync wait begin/observed/timeout event。
- ARGB/XRGB mid 与 after 的三个完整图标均为 expected dx = measured dx = 20 px、
  `SAD=0`、exact match = 1.0。
- framebuffer capture 未见 horizontal offset 或 mixed-generation regression。
- framebuffer 数据不能替代物理面板观察；normal scroll、fast fling 的 tearing/flicker 肉眼结论仍待人工确认。

## Priority after phase one

```text
P0  pure render
    ARGB avg 83.37 ms; XRGB avg 75.61 ms

P1  large dirty area
    280,064 pixels/frame; 72.93% of screen

P2  ARGB blend overhead
    about 10.3% versus XRGB in this run

Not current P1  steady-state DIRECT sync
    zero copied pixels in 11/12 scroll frames
```

按照第一阶段停止条件，本次不继续修改 LVGL renderer、DMA2D dispatch threshold、Launcher
视觉效果或 framebuffer sync 实现。进入第二阶段前，应先针对 image/fill/text/shadow/mask 与
DMA2D wait 做 draw-task breakdown。

## Reproduction

```sh
cmake --preset Debug-LTDC-Sync-Trace
cmake --build --preset Debug-LTDC-Sync-Trace
openocd -f CartDeck.cfg
python3 tools/debug/capture_scroll_frames.py --modes argb xrgb
```

## Check results

- HostTest：15/15 PASS。
- `Debug`、`Release`、`SizeDebug`、`Debug-USB-SD-MSC`、
  `Debug-LTDC-Sync-Trace`、`SizeDebug-DMA2D-SelfTest`：build PASS。
- DMA2D L1 R2M、L2 M2M tight、L3 M2M strided、L4 PFC：目标板均为
  `state=2`、`pass=1`、`fail=0`、CFSR=0、HFSR=0。
- deterministic timing 与 framebuffer capture：ARGB/XRGB PASS。
- 尚未执行 5 分钟人工滚动、normal scroll / fast fling 肉眼检查，因此不把 tearing、flicker
  或主观 jitter 标记为已验证。

## Referenced files

- `Core/APPS/LVGL/port/lv_port_disp.c`
- `Core/APPS/LVGL/src/core/lv_refr.c`
- `Core/Debug/display_trace.c`
- `Core/Debug/display_trace.h`
- `Core/Driver/LCD/lcd.c`
- `tools/debug/capture_scroll_frames.py`
- `tools/gdb/ltdc_sync_trace.gdb`
- `Docs/display/LTDC_LVGL_SYNC_AUDIT.md`
- `Docs/display/FRAMEBUFFER_CAPTURE_ANALYSIS.md`
