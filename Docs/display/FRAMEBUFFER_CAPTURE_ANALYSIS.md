# Launcher deterministic scroll framebuffer analysis

## 状态与结论

2026-09-28 在 STM32H743 目标板完成第一轮 `ARGB8888` / 同像素 `XRGB8888`
deterministic 横向滚动抓帧。当前结论为：

> **ROOT CAUSE NOT CONFIRMED；现有证据不支持 LVGL 9.6 DMA2D image blend/BGOR
> correctness 是当前横向错位的首要原因。**

ARGB 与 XRGB 的 before/mid/after 两块 framebuffer 都是完整、内部一致的 frame；mid
相邻 generation 的三个完整图标均精确移动 20 px，逐采样像素 `SAD=0`、exact match=1.0。
未发现单个 framebuffer 内部的水平断层、额外位移或 mixed-generation splice。相同 scroll
位置的 ARGB 与 XRGB 图标区域逐像素一致。下一步应优先继续 LTDC/panel presentation 与
frame pacing 调查；暂不进入 production renderer 修改，也没有足够理由进入 standalone
alpha/BGOR 修复。

本报告不声称已在 deterministic run 中同步拍摄 LCD 面板。raw framebuffer 可以否定“抓取
时该 buffer 已经画坏”，但不能单独证明肉眼异常只存在于 scanout。

## Capture method

`Debug-LTDC-Sync-Trace` 才编入以下诊断逻辑，普通 Debug / Release / SizeDebug 不改变：

```text
GDB scalar mailbox
  -> Launcher_Task() (app/LVGL owner task)
  -> real Launcher box_container
  -> lv_obj_scroll_to_x(..., LV_ANIM_OFF)
  -> wait for the corresponding LVGL render frame
```

固定场景为 `+240 px / 12 steps / 20 px per rendered frame`。GDB 从不调用 LVGL API，
也不写 `lv_obj_t` 内部字段。Timing run 不在中途 halt；capture run 在 app task 发布
`g_fb_capture_ready` 后 halt，并同时 dump：

```text
FB_A 0xD0177000..0xD02EE000
FB_B 0xD02EE000..0xD0465000
```

每个 raw 文件必须为 1,536,000 bytes。自动入口：

```sh
cmake --preset Debug-LTDC-Sync-Trace
cmake --build --preset Debug-LTDC-Sync-Trace
python3 tools/debug/capture_scroll_frames.py --modes argb xrgb
```

`tools/debug/capture_scroll_frames.py` 生成独立 session，运行 timing/capture 两阶段并调用
`tools/debug/framebuffer_to_png.py`。后者只用 Python 标准库，保留 raw 证据，输出 raw-alpha、
opaque PNG、diff、row/column profile、icon crops、tear candidates、`analysis.json` 与
`summary.md`。`tools/gdb/ltdc_sync_trace.gdb` 另提供手动的 `ltdc_scroll_prepare`、
`ltdc_scroll_start`、`ltdc_scroll_status`、`ltdc_capture_metadata` 和 `ltdc_capture_fb`。

GDB halt 会改变时序，因此 capture run 只用于 pixel-state inspection；性能与 reload 统计只取
uninterrupted timing run。

## Byte order calibration

LVGL 的 ARGB8888 数值语义为 `0xAARRGGBB`，STM32H743 为 little-endian，因此 raw memory
中的四字节顺序是 `B,G,R,A`。诊断构建在 `(0,0)..(19,3)` 绘制 red/green/blue/white/black
五个 4x4 色块。ARGB 与 XRGB 的 12 个 raw 文件都通过中心像素校准，确认 converter 使用：

```text
BGRA raw bytes -> RGBA PNG channels
```

转换过程一 raw pixel 对应一 PNG pixel，不 resize、不插值。opaque preview 只将输出 alpha
设为 255；raw-alpha PNG 保留 framebuffer alpha。

## Timing run

固件 ELF SHA-256：
`9d3142e4cc04cded31e9c84cb0810af375c5c78ba9a9643250ed974a1ef5f580`。

| Mode | Render frames | Image tasks | DMA2D mode | Render min / avg / max | Reload request/event/complete | Errors |
|---|---:|---:|---|---|---|---|
| ARGB | 12 | 46 ARGB | 46 blend, 0 PFC | 84.92 / 91.48 / 101.91 ms | 12 / 12 / 12 | timeout=0, ownership=0, LTDC=0 |
| XRGB | 13 | 50 XRGB | 0 blend, 50 PFC | 53.05 / 83.00 / 91.75 ms | 13 / 13 / 13 | timeout=0, ownership=0, LTDC=0 |

时间由 DWT `CYCCNT` / 480 MHz 换算。XRGB 本轮平均 render window 约快 9%，说明 ARGB blend
可能是 performance amplifier；样本数不同且仍包含旧 VSync busy-wait，不能把该差异解释为
DMA2D correctness bug 或最终性能结论。

## DMA2D target configuration

ARGB 与 XRGB 的几何、source buffer、stride、clip 均相同；区别仅为 descriptor 与硬件 mode。

| Scene | Visible width | FGOR | BGOR | OOR | NLR | ARGB mode | XRGB mode |
|---|---:|---:|---:|---:|---|---|---|
| fully visible | 200 | 0 | 600 | 600 | `200 x 200` | M2M_BLEND | M2M_PFC |
| left clipped | 20 | 180 | 780 | 780 | `20 x 200` | M2M_BLEND | M2M_PFC |
| right clipped | 180 | 20 | 620 | 620 | `180 x 200` | M2M_BLEND | M2M_PFC |

输出 stride 为 3200 bytes / 800 pixels，source stride 为 800 bytes / 200 pixels，`LOM=0`。
实际 snapshot 中 `BG==OUT`，即 production 使用 in-place background/output。上述 offset 与
visible width 一致，没有发现 clipping-specific BGOR/OOR/NLR 参数错误。

## Framebuffer results

Session：`build/Debug-LTDC-Sync-Trace/captures/20260928_argb_smoke/`。该目录位于 build
输出下，不提交 Git；以下 SHA-256 使原始证据可追踪。

| Capture | FB | Content frame | Scroll x | Pixel correctness | Unexpected offset | Raw SHA-256 |
|---|---|---:|---:|---|---:|---|
| ARGB before | A | 0 | 0 | PASS | 0 | `ea87c3c76ad911155eb82b4cab3fd9211aa9dd8e65b0d4900f89143eda2e9225` |
| ARGB before | B | 0 | 0 | PASS | 0 | `d124cf38c909288730ef9a2b5710579539896db0e99a235f633c9805f08c8003` |
| ARGB mid | A | 5 | 100 | PASS | 0 | `69c62ae936ce7d0ca7f13a7e77c18780b7ee96ffd2de68cf2b472933dbafbb4f` |
| ARGB mid | B | 6 | 120 | PASS | 0 | `93b4702a29b7bcb881d4a36ba6d8ada6d0a0e42ca2452ff9fd335c53df8d21fa` |
| ARGB after | A | 13 | 240 | PASS | 0 | `cefbf9406051c170c3259aceefbcc7576a9798370132b629ca8cf97c7a5ba8bf` |
| ARGB after | B | 12 | 220 | PASS | 0 | `020d0bb925252363b97bf6df22452df3db66bc4f08ad9554878dcdec842ea8d3` |
| XRGB before | A | 1 | 0 | PASS | 0 | `74f807960f30dad4afd658482bb30ca9957c084b86b36cbd27b20c0c8e30477e` |
| XRGB before | B | 0 | 0 | PASS | 0 | `ee273a84fc7f0cc17b8ff1b1dafeec677707e6ac4d91e37d207fb44a7d6d446c` |
| XRGB mid | A | 7 | 120 | PASS | 0 | `5e91540985f81573d7a2e8e242fb265fefd2b605155ae9a2eeb213859da0f18e` |
| XRGB mid | B | 6 | 100 | PASS | 0 | `d2a9e16850b46b07bd27486d764e34e77326b94b4e57bbab97cb07bfdcf057dd` |
| XRGB after | A | 13 | 220 | PASS | 0 | `bf65706e63ecdcd143822c72b07ded2b4e7f5957a14f52ae5e12ba68f323ffce` |
| XRGB after | B | 14 | 240 | PASS | 0 | `8177119b52d36e207687ce47c598031168520337ad5000c38478692f0cacf559` |

before 的 content sequence 在 trace reset 边界为 0，但 raw 中是校准通过的完整初始画面；
mid/after 的 generation 与 scroll x 则由每次 render 后的 mailbox 明确记录。

| Mode / pair | Expected screen dx | Measured icon dx | Exact match | Abnormal region |
|---|---:|---:|---:|---|
| ARGB mid A(100) -> B(120) | -20 | -20（slot 1/2/3） | 1.0，SAD=0 | none |
| XRGB mid B(100) -> A(120) | -20 | -20（slot 1/2/3） | 1.0，SAD=0 | none |
| ARGB after B(220) -> A(240) | -20 | -20（slot 1/2/3） | 1.0，SAD=0 | none |
| XRGB after A(220) -> B(240) | -20 | -20（slot 1/2/3） | 1.0，SAD=0 | none |

mid 的 A/B diff bbox 都为 `[0,4,799,305]`，changed pixels 分别为 ARGB 154,717、
XRGB 154,716；peak row 均为 y=108 / 782 pixels。变化从图标区域顶部开始并覆盖正常水平移动
区域，没有出现某个 y 之后突然切换为另一 generation 的 row profile。after 两组也同为
`[0,4,681,305]` / 130,762 changed pixels。

ARGB scroll=120 与 XRGB scroll=120 的交叉比较只有 73 个全屏像素不同，bbox
`[0,4,710,456]`；差异来自 frame marker 与底部动态状态文字。三个图标 region 的
`best_dx=0`、`SAD=0`、exact match=1.0。

## Required answers

1. synthetic scroll 完全由 `Launcher_Task()` 执行；GDB 只写 scalar mailbox。
2. 参数与每步目标位置由整数公式确定，可 100% 重放相同 12 steps。
3. FB_A raw 全部为 1,536,000 bytes。
4. FB_B raw 全部为 1,536,000 bytes，校准与 consistency checks 通过。
5. ARGB8888 是 `0xAARRGGBB` 数值；little-endian raw 为 BGRA。
6. 两种模式、两块 buffer 的 RGB calibration 全部 PASS。
7. ARGB mid framebuffer 未发现内部错位。
8. XRGB mid framebuffer 未发现内部错位。
9. raw framebuffer 中没有异常额外 horizontal offset。
10. 历史肉眼异常是否只存在于 LCD scanout仍未由同步面板照片确认。
11. mid generation：ARGB A=5/B=6；XRGB A=7/B=6。
12. 可比较图标的实际 screen displacement 是 20 px。
13. measured dx 与 synthetic theoretical dx 完全一致。
14. 两个 buffer 是相邻完整 generation，不是单 buffer 上下拼接。
15. row profile 没有发现明确 y 位置后的 generation 突变。
16. 本轮未见 DIRECT dirty sync 产生 mixed-generation framebuffer。
17. ARGB/XRGB diff geometry 基本相同，图标像素结果一致。
18. DMA2D blend correctness 不再是首要嫌疑；它仍可能放大 render cost。
19. LTDC/panel presentation 或 frame pacing 是下一步优先方向，但根因尚未确认。
20. 下一分支：`LTDC/panel presentation`，并将旧 VSync busy-wait 作为独立 jitter 变量。

## Referenced files and checks

- `Core/Screen/Page/ui_screen_launcher.c`
- `Core/Debug/display_trace.h`
- `Core/Debug/display_trace.c`
- `Core/APPS/LVGL/src/draw/dma2d/lv_draw_dma2d_img.c`
- `Core/APPS/LVGL/src/draw/dma2d/lv_draw_dma2d_fill.c`
- `Core/APPS/LVGL/src/draw/sw/lv_draw_sw_img.c`
- `tools/gdb/ltdc_sync_trace.gdb`
- `tools/debug/capture_scroll_frames.py`
- `tools/debug/framebuffer_to_png.py`

`Debug-LTDC-Sync-Trace` 与普通 `Debug` 均构建通过；Python scripts 通过 `py_compile`，
`git diff --check` 通过。capture 产物保留在 ignored build 目录，没有加入版本控制。
