# Launcher 双 LTDC 图层硬件平移

Launcher 滚动不再经过 LVGL 每帧 rasterize：固定 UI 与滚动内容各自烘焙成一张
SDRAM 缓存，滚动只改 HW Layer1 的 source 起始地址（CFBAR），由 LTDC 在 VBlank
原子生效。

关联文件：

- `Core/Driver/LCD/ltdc_layer.{h,c}` — 图层几何/pitch/地址的 NoReload 封装
- `Core/Driver/LCD/ltdc_reload.{h,c}` — VBlank reload 所有权登记（纯逻辑）
- `Core/Screen/Page/launcher_hw_pan.{h,c}` — pan 状态机 + coalescing（纯逻辑）
- `Core/Screen/Page/display_mode.{h,c}` — 显示模式状态机（纯逻辑）
- `Core/Screen/Page/launcher_cache_render.{h,c}` — LVGL snapshot → SDRAM 缓存
- `Core/Screen/Page/launcher_display.{h,c}` — 模式/缓存/图层编排
- `Core/Screen/Page/ui_screen_launcher.c` — Launcher 集成点
- `Core/Inc/sdram_layout.h` — 缓存地址与 strip 几何
- `tests/host/launcher_hw_pan_test.c` — 纯逻辑回归
- `tools/debug/analyze_ltdc_layers.py` — 双层 composite 重建与对比

## 1. 冻结的硬件图层映射

| 名称 | HAL LayerIdx | 寄存器块 | 用途 |
|---|---|---|---|
| **HW Layer0** | 0 | `LTDC_Layer1` @ 偏移 `0x84` | Launcher 固定 UI（单缓冲，低频更新）；Lua 模式下 **disabled** |
| **HW Layer1** | 1 | `LTDC_Layer2` @ 偏移 `0x104` | Launcher strip（硬件平移）或 LVGL DIRECT 双缓冲 |

`stm32h7xx_hal_ltdc.h` 的 `LTDC_LAYER(hltdc, idx)` = `base + 0x84 + 0x80*idx`，
所以 HAL 的 "LTDC_Layer1" 名字对应的是**硬件 Layer0**。本仓库不再使用
`Layer1_FB0` / `Layer2_FB0` 之类命名表达硬件层。

LTDC 固定混合顺序是 Layer1 覆盖 Layer0，因此要显示的滚动内容必须放在
**HW Layer1**，固定 UI 放在 **HW Layer0**。

## 2. 两种显示模式

| 模式 | HW Layer0 | HW Layer1 |
|---|---|---|
| `DISPLAY_MODE_LAUNCHER_HW_PAN` | enabled，全屏，静态 UI framebuffer | window `x 0..799, y 26..329`，pitch 10656，source = strip + scroll_x*4 |
| `DISPLAY_MODE_LVGL_APP` | **disabled** | 全屏 `800x480`，pitch 3200，CFBAR = FB_A / FB_B |

过渡态 `TO_LAUNCHER_HW_PAN` / `TO_LVGL_APP` 表示"正在准备一次 atomic latch"。
`display_mode_lvgl_refresh_allowed()` 只在稳定 `LAUNCHER_HW_PAN` 返回 false ——
两个过渡态都必须允许 LVGL 刷新，否则回到 Lua 的第一帧永远无法产生。

## 3. SDRAM 缓存

沿用 `Core/Inc/sdram_layout.h` 的固定布局，不新增 region、不动态分配：

| 缓存 | 地址 | 尺寸 | stride |
|---|---|---|---|
| HW Layer0 静态 UI（front） | `SDRAM_LAYER0_FB_BASE` = `0xD0000000` | 800×480 | 3200 |
| HW Layer0 静态 UI（staging back） | `SDRAM_LVGL_FB_A_BASE` = `0xD0177000` | 800×480 | 3200 |
| Launcher strip | `SDRAM_LAUNCHER_STRIP_BASE` = `0xD0465000` | 2660×350 | 10656 |

Launcher 模式下 LVGL 不参与 scanout，因此 `LVGL_FB_A` 可以安全借作固定层的
staging back：先在 back 上完成 snapshot，再用 Layer0 `CFBAR` + VBR 一次性切换，
固定层的低频更新同样无 tearing。`layer_info[1]` 始终代表 LVGL 双缓冲，不会被
硬件平移改写。

## 4. strip stride

```
逻辑行字节 = 2660 * 4            = 10640 (0x2990)
物理 stride = align_up(10640, 32) = 10656 (0x29A0) = 2664 px
实际分配   = 10656 * 350          = 0x38E8C0 (3,729,600 B)
arena      = 0x400000 (4 MiB)，余量 0x71740
```

10640 不是 32 的倍数，直接用会让每行 2D 传输与 cache line 边界错位。尾部 4 个
像素只是对齐填充，**不属于可见内容**，因此最大平移量仍是

```
LAUNCHER_SCROLL_MAX_X = 2660 - 800 = 1860      （不是 2664 - 800 = 1864）
```

本项目 `LV_DRAW_BUF_STRIDE_ALIGN = 32`，所以
`lv_draw_buf_width_to_stride(2660, ARGB8888) == 10656`，与预留完全一致 ——
这也是能直接用公开 `lv_snapshot` API 写进 SDRAM 而不需要额外临时缓冲的原因。

## 5. LTDC 寄存器语义（已核对 HAL 源码）

`LTDC_SetConfig()`（`stm32h7xx_hal_ltdc.c:2137-2197`）的编码是：

```
WHPCR = (WindowX0 + AHBP + 1) | ((WindowX1 + AHBP) << 16)
WVPCR = (WindowY0 + AVBP + 1) | ((WindowY1 + AVBP) << 16)
CFBAR = FBStartAdress
CFBLR = (ImageWidth * bpp) << 16 | ((WindowX1 - WindowX0) * bpp + 7)
CFBLNR = ImageHeight
```

关键点：**HAL 的 `ImageWidth` 字段表达的是 pitch（像素），不是可见宽度**。
所以"可见 800 / pitch 10656"用公开 HAL API 就能表达：

| 目标 | 配置 | 结果 |
|---|---|---|
| pitch 10656 B | `ImageWidth = 2664`, `bpp = 4` | `CFBP = 0x29A0` |
| 可见 800 px | `WindowX0 = 0`, `WindowX1 = 800` | `CFBLL = 800*4+7 = 0xC87` |
| 行数 | `ImageHeight = 304` | `CFBLNR = 304` |

**不需要散写任何寄存器**，全部走 `HAL_LTDC_ConfigLayer_NoReload()`；steady-state
pan 只走 `HAL_LTDC_SetAddress_NoReload()`。没有任何 `SRCR.IMR` 使用。

目标板实测（`Debug-Launcher-HW-Pan`，scroll_x = 0）：

```
HW L0 (0x84)  CR=0x00000001 CFBAR=0xD0177000 CFBLR=0x0C800C87
HW L1 (0x104) CR=0x00000001 CFBAR=0xD0465000 CFBLR=0x29A00C87
              CFBLNR=304 WHPCR=0x034E002F WVPCR=0x01610032
```

`WVPCR = 0x01610032` → start 50 = 26 + AVBP(24)，stop 353 → 304 行，与窗口一致。

## 6. 窗口高度为什么是 304

strip 窗口起始行必须等于 content viewport 顶边，否则 source 行会整体错位：

```
window_y = BOX_CONTAINER_Y = 26
```

高度则受"谁覆盖在 strip 之上"约束：5 个固定圆按钮位于内容视口带**之内**
（`CIRCLE_Y = 330 < 26 + 350 = 376`），而 HW Layer1 在 HW Layer0 之上且第一版
为不透明。如果窗口覆盖到圆按钮，圆按钮会被裁掉。因此：

```
window_height = CIRCLE_Y - window_y = 330 - 26 = 304
```

strip 仍然完整渲染 2660×350（槽位在 strip 行 80..279，全部落在可见的 0..303
内），只是末尾 46 行不参与扫描。这是从真实 UI 几何推导的，不是硬编码 26/350。

## 7. 缓存构建

两张缓存都用 LVGL 公开 offscreen API 生成，不改 LVGL renderer/refresh 核心：

```c
lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_ARGB8888, stride, SDRAM_ADDR, stride * h);
lv_snapshot_take_to_draw_buf(obj, LV_COLOR_FORMAT_ARGB8888, &buf);
```

`lv_draw_buf_init` 把 `header.flags` 置 0，因此 LVGL 不会释放 SDRAM 固定区间。

- **Layer0 静态层** = `lv_snapshot_take_to_draw_buf(s_main_container)`，渲染期间
  临时隐藏 `box_container`（滚动内容根），并临时关闭 display invalidation 避免
  污染 scanout 和产生无意义重绘。
- **strip** = `lv_snapshot_take_to_draw_buf(content_container)`。

`LV_USE_SNAPSHOT` 在 `Core/APPS/LVGL/lv_conf.h` 打开（模块开关，非核心补丁）。

构建前置检查：对象尺寸必须等于目标缓存尺寸、`lv_obj_get_ext_draw_size() == 0`
（缓存没有外延余量），任一不满足直接返回错误码并走 fallback，不做截断渲染。

### 时机与 dirty 策略

- 只在 icon cache / app 名 / slot 对象全部 READY 后构建，避免把 LOADING 占位
  图烘焙进 strip。
- `launcher_display_mark_strip_dirty()`：App 安装/删除、排序、icon 变化、
  app 名变化、slot 视觉变化、需要烘焙的 selection 变化。
- `launcher_display_mark_static_dirty()`：状态栏、圆按钮视觉、divider、
  背景/主题变化。
- **滚动绝不置 dirty**，也绝不在 pan 期间修改 strip 内容。

## 8. pan 状态机与 coalescing

```
desired_x   最新请求位置
requested_x 已写入 CFBAR shadow 的位置
latched_x   已由 ReloadEvent 确认生效的位置
pending     是否有未完成的 VBR
```

- `set_x()`：pending 时只更新 `desired_x` 并计入 `coalesced`，**不再发第二个 VBR**
- `ReloadEvent` → `latched_x = requested_x`，清 pending
- 下一次 tick：若 `desired_x != latched_x` 才提交最新位置

即 **latest-position-wins**：不试图显示每一个触摸采样，每个 VBlank 显示当时最新
的位置，最大 cadence ≈ 58.878 Hz。

`launcher_hw_pan_set_x()` / `launcher_hw_pan_tick()` 都可被 app task 安全调用；
ISR 只做 `launcher_hw_pan_on_reload_complete()`（纯标量状态迁移）。

## 9. reload 所有权

LTDC 只有一个 `SRCR.VBR` 位和一个 RR 事件，但有多个独立提交者。因此任何 VBR
在写 SRCR 前必须先登记 owner：

| owner | 提交者 |
|---|---|
| `LTDC_RELOAD_OWNER_LVGL_FLUSH` | LVGL DIRECT flush 切换 HW Layer1 CFBAR |
| `LTDC_RELOAD_OWNER_LAUNCHER_PAN` | Launcher 硬件平移 |
| `LTDC_RELOAD_OWNER_LAYER0_SWAP` | HW Layer0 静态层 front/back 交换 |
| `LTDC_RELOAD_OWNER_MODE_SWITCH` | 模式切换 / 启动期几何 latch |

`HAL_LTDC_ReloadEventCallback()`（`Core/Driver/LCD/lcd.c`）按 owner 分派：

- `LVGL_FLUSH` → `lv_port_disp_signal_reload_complete()`
- 其它 → `ltdc_reload_notify_external()` → `launcher_display` 的 ISR 入口
- `NONE`（未登记）→ 计数 `orphan_completions`

这样一次 Launcher pan 的 ReloadEvent **不会**误执行 LVGL flush 的完成回调，避免
确认一个不存在的 LVGL frame。`LVGL_APP` 模式下的 request_seq / complete_seq /
generation / semaphore / flush_wait 语义完全保持不变。

`drv_display` 不能反向链接 `app_screen`，所以外部 owner 的消费者通过
`ltdc_reload_set_external_dispatch()` 注册，不产生链接环。

## 10. 模式切换序列

### LVGL_APP → LAUNCHER_HW_PAN

1. 等没有未完成的 reload（否则本 tick 拒绝，下一次重试）
2. 等 icon / app 名 / slot READY
3. 若 strip/static dirty：各自 snapshot 到 SDRAM 缓存（失败 → abort + fallback）
4. `HAL_LTDC_ConfigLayer_NoReload` 配置 HW Layer0 静态层 与 HW Layer1 strip 窗口
5. 关闭 LVGL display invalidation（对象/输入/timer 全部保留可用）
6. `ltdc_reload_arm(MODE_SWITCH)` + 一次 `SRCR.VBR`
7. ReloadEvent 确认后 commit → `LAUNCHER_HW_PAN`，使能 pan

### LAUNCHER_HW_PAN → LVGL_APP

1. 停用 pan，等最后一笔 pan VBR 落地
2. 重新允许 invalidation，并强制全量失效以驱动第一张 Lua full frame
3. 第一张 full frame 的 LVGL flush 通过 `lv_port_disp_set_pre_latch_hook()` 在
   **同一次 VBR 内**恢复 HW Layer1 全屏几何并关闭 HW Layer0
4. 该 flush 完成（owner = `LVGL_FLUSH`）后 commit → `LVGL_APP`

第 3 步是关键：如果先把 HW Layer1 切到旧 FB_A 再等 Lua 慢慢画，会先显示旧
Launcher 画面或几何错乱的一帧。现在 Lua 第一帧就绪即原子切换。

### 防御性门控

`lv_port_disp_set_presentation_gate()`：`LAUNCHER_HW_PAN` 稳定态下 disp_flush
只结束 flush，**不修改 CFBAR、不请求 VBR**。即使有 invalidation 漏网，也不会
破坏 strip 几何。

## 11. fallback

编译期开关 `CARTDESK_LAUNCHER_HW_PAN`（CMake option，默认 **OFF**）。关闭时
`launcher_display_request_hw_pan()` 直接返回 false 并记录 `DISABLED`，生产
slot-local LVGL 路径完全不变。

运行期 fallback 原因（`launcher_fallback_reason_t`）：

| 原因 | 触发条件 |
|---|---|
| `DISABLED` | 编译期开关关闭 |
| `GEOMETRY` | UI 几何不满足双图层前提 |
| `CACHE_BUILD` | strip / 静态层 snapshot 失败（尺寸、ext_draw、stride、API） |
| `ICON_TIMEOUT` | icon 未 READY（会持续重试，不进入半初始化模式） |
| `BUSY` | 切换被占用或 VBR 被拒 |
| `LTDC_CONFIG` | HAL 图层配置返回错误 |

任一失败都 `display_mode_abort()` 回 `LVGL_APP`，不进入半初始化硬件平移模式。

## 12. 性能

目标板实测（`Debug-Launcher-HW-Pan`，STM32H743 @480MHz）：

| 指标 | 数值 |
|---|---|
| strip 烘焙 | 196 ms（一次性） |
| 静态层烘焙 | 76 ms（一次性） |
| steady-state 每 VBlank CPU | 一次 CFBAR 写 + 一次 VBR 请求 |
| pan 期间的 LVGL render / draw task / pre-clear / DMA2D | 0 |
| pan 更新率 | 3553 次 / 60 s ≈ **59.2 Hz** ≈ 1 次/VBlank |
| VBR 握手失败 | 0（mismatch / rejected / orphan / overrun / crosstalk 全 0） |
| LTDC ISR | `0x00000000`（无 FUERR / TERR / 其它错误） |
| CPU fault | `CFSR = HFSR = 0` |

`CFBLR` / `CFBLNR` 在整个 pan 过程中保持 `0x29A00C87` / `304` 不变 ——
steady-state 只写 CFBAR。

`tools/debug/analyze_ltdc_layers.py` 可以按 LTDC 的真实合成规则重建面板图，
并检查越界/padding/窗口对齐。

### 目标板实测：模式切换（50 次往返）

`Debug-Launcher-HW-Pan` 上由 `g_launcher_hwpan_lua_cycle_target` 驱动的自动往返
（真实的 Lua 启停路径，非桩）：

| 指标 | 数值 |
|---|---|
| Launcher → Lua → Launcher 完整往返 | **50 / 50** |
| 切换 abort | **0** |
| reload owner 一致性 | `orphan=0 overrun=0 crosstalk=0`，arms 与 completions 完全匹配 |
| strip / 静态层重建 | 各 2 次（进入 + 返回），0 失败，99 ms / 40 ms |
| `dm_rejected_busy` / `dm_rejected_invalid` | 0 / 0 |
| 最终状态 | `LAUNCHER_HW_PAN`，L1 回到 strip（`CFBLR=0x29A00C87`，`CFBLNR=304`） |

单次往返期间采到的中间态证明两层配置是原子切换的：

| 阶段 | mode | HW L0 CR | HW L1 CFBAR / CFBLR / CFBLNR |
|---|---|---|---|
| Launcher | `LAUNCHER_HW_PAN` | `0x1`（enabled） | `0xD0465000` / `0x29A00C87` / 304（strip） |
| Lua Cart | `LVGL_APP` | **`0x0`（disabled）** | `0xD02EE000` / `0x0C800C87` / 480（FB_B 全屏） |
| 返回 Launcher | `LAUNCHER_HW_PAN` | `0x1`（enabled） | `0xD0465000` / `0x29A00C87` / 304（strip） |

LTDC `ISR` 在整个压力过程中保持 `0x00000000`。

## 13. Debug 面与 Release 面

`PERF_MONITOR_ENABLE`（Debug / RelWithDebInfo 为 1）是唯一的 debug 面开关：

- Debug 面额外保留 GDB 快照 `g_launcher_hwpan_debug`、确定性单步口
  `g_launcher_hwpan_request_x`、10k pan 压力驱动器 `g_launcher_hwpan_stress_*`
  以及 Launcher↔Lua 往返驱动器 `g_launcher_hwpan_lua_cycle_*`。
- Release 面这些符号**全部不存在**，只保留 display mode manager、strip 渲染器、
  hardware pan 逻辑与 cache 重建路径。

可用 `arm-none-eabi-nm build/Release/cartdesk-os.elf` 复核。

## 14. 已知边界与未完成项

- **触摸映射**：硬件平移后 slot 的 screen-space 位置由 `touch_x + logical_scroll_x`
  推导，不能用旧的 LVGL object coords 做 hit-test。本任务按规划保留第一版
  deterministic 验证路径（GDB 驱动），生产触摸映射属于后续工作。
- **strip rebuild 的安全窗口**：当前要求 strip dirty 时不在扫描中原地改写；低频
  rebuild 的完整状态机（停 pan → 等 VBR → 重建 → 重新 latch）尚未实现，现阶段
  rebuild 都发生在进入 Launcher 模式之前（含从 Lua 返回时的重建）。
- `Docs/display/SDRAM_LAYOUT.md` 描述 SDRAM 分区，本文档描述图层与平移行为。
