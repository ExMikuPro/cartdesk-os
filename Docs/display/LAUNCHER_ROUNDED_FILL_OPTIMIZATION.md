# Launcher 大面积圆角填充验证

## 结论

**ROUNDED FILL NOT WORTH STRUCTURAL COMPLEXITY**

目标板三轮重复 A/B 表明，将热点对象的 radius 从 10 改为 0 后，虽然任务会从软件渲染切换到 DMA2D R2M，但平均 `T_render` 只从 57.09 ms 降到 56.46 ms，净收益 0.62 ms/frame，低于本阶段规定的 2–3 ms 停止门槛。因此没有实现 decomposition，也没有修改 production 对象结构。

## 测试条件

- STM32H743 @ 480 MHz
- LVGL 9.6.0，800×480 ARGB8888，DIRECT 双 framebuffer
- `Debug-LTDC-Full-Render-Audit`
- Phase 3 DMA2D pre-clear 保持开启，`g_render_audit_preclear_mode=3`
- DMA2D async 保持关闭
- 每轮冷启动后执行 `+240 px / 12 steps / 20 px` deterministic scroll
- normal 与 radius=0 各重复三轮

## 热点对象

目标板在热点 SW draw task 上读取 `lv_draw_fill_dsc_t::base.obj`，并与 Launcher 注册的 debug object ID 1 比较：

| 字段 | 结果 |
|---|---|
| Debug object ID | 1，`Launcher content background` |
| LVGL object | `0x240023ec`，class `lv_obj`（地址仅对该次运行有效） |
| Parent | `0x2400227c`，class `lv_obj`，即 `box_container` |
| Source | `Core/Screen/Page/ui_screen_launcher.c:1207` |
| Object geometry | 2660×350；采样时 `(-20,26)–(2639,375)` |
| Parent geometry | 800×350；`(0,26)–(799,375)` |
| Clip area | `(0,26)–(799,375)` |
| Effective pixels | 280,000 |
| Radius | 10 px |
| Radius / width | 0.376% |
| Radius / height | 2.857% |
| Background | `#FFFFFF`, opacity 255，no gradient |
| Reproduced normal cycles | 4,632,347 cycles/frame average，9.65 ms/frame |
| Historical full-audit result | 5,899,114 cycles，12.29 ms/frame |

运行时探针输出中的 `matches=1` 证明 draw descriptor 对象与 Launcher 注册对象完全一致，不是根据截图或矩形尺寸推测。

## DMA2D 拒绝原因

`Core/APPS/LVGL/src/draw/dma2d/lv_draw_dma2d.c` 的 `evaluate_cb()` 对 fill 明确执行：

```c
if(dsc->radius != 0) audit_reject |= 0x002u;
if(dsc->grad.dir != LV_GRAD_DIR_NONE) audit_reject |= 0x004u;
```

热点任务实测 radius=10、无渐变、ARGB8888 target，reject mask 为 `0x002`。因此拒绝原因已确认是 radius，不是 opacity、gradient、format 或截图推断。

## Radius=0 A/B

Debug-only mailbox 只改变 debug object ID 1 的 `LV_STYLE_RADIUS`。geometry、颜色、opacity、children、scroll 与 Phase 3 pre-clear 均不变。Release 等非 Full Render Audit 构建不会编译该 mailbox。

| Run | Normal hotspot SW | Radius=0 hotspot R2M | Normal T_render | Radius=0 T_render | 净收益 |
|---|---:|---:|---:|---:|---:|
| 1 | 9.6525 ms | 8.7983 ms | 57.0320 ms | 56.5314 ms | 0.5006 ms |
| 2 | 9.6629 ms | 8.8109 ms | 57.1813 ms | 56.4132 ms | 0.7681 ms |
| 3 | 9.6368 ms | 8.8105 ms | 57.0511 ms | 56.4501 ms | 0.6010 ms |
| Mean | **9.6507 ms** | **8.8066 ms** | **57.0881 ms** | **56.4649 ms** | **0.6232 ms** |

Radius=0 后，热点记录从 `unit=SW, coverage=blend` 变为 `unit=DMA2D, coverage=opaque`。R2M task count 每次 14 帧增加 14，R2M pixels 增加 3,920,000，恰好等于 `14 × 280,000`。

## 成本转移

三轮平均：

| Metric | Normal | Radius=0 | Delta |
|---|---:|---:|---:|
| Rounded task | 9.6507 ms SW | 8.8066 ms R2M | -0.8441 ms |
| Total SW execute | 20.2476 ms | 10.5164 ms | -9.7312 ms |
| DMA2D R2M active | 4.6629 ms | 12.4091 ms | +7.7462 ms |
| DMA2D wait | 23.7325 ms | 31.6551 ms | +7.9226 ms |
| Cache | 1.2736 ms | 2.3436 ms | +1.0700 ms |
| T_render | 57.0881 ms | 56.4649 ms | -0.6232 ms |

软件执行下降并不等于帧时间收益。新增的大面积 R2M 与 LTDC 扫描争用 SDRAM，且同步 backend 等待 DMA2D 完成；额外 wait 与 cache 成本抵消了绝大部分软件填充下降。

## Framebuffer 与 decomposition

Radius=0 是性能诊断，不是视觉等价候选；它会按预期改变四角像素。由于净收益只有 0.62 ms/frame，已触发 `<2–3 ms` 停止条件，因此未进入 decomposition、corner AA framebuffer diff 或 stress 阶段。不存在可以提交为 production 的新几何，production 视觉保持原状。

## 最终报告表

| Version | Rounded SW | DMA2D fill | DMA2D wait | T_render | Presentation FPS |
|---|---:|---:|---:|---:|---:|
| Historical baseline | 12.29 ms | 4.36 ms | 22.10 ms | 57.12 ms | ~15 |
| Reproduced normal, 3-run mean | 9.65 ms | 4.66 ms | 23.73 ms | 57.09 ms | ~15，4 panel frames |
| Radius=0 test, 3-run mean | 0 ms（热点） | 12.41 ms | 31.66 ms | 56.46 ms | ~15，4 panel frames |
| Decomposed | 未执行：未达到继续门槛 | — | — | — | — |

## 20 个问题

1. 12.29 ms 对应哪个对象？Launcher 横向滚动区的 `content_container` 背景，debug ID 1。
2. 对应源码哪一行？`Core/Screen/Page/ui_screen_launcher.c:1207` 创建。
3. 它的尺寸？对象 2660×350；屏幕有效区域 800×350。
4. radius 多少？目标板实测 10 px。
5. 为什么 DMA2D 拒绝？`radius != 0`，reject mask `0x002`。
6. radius=0 是否切到 DMA2D？是，任务由 unit 2/SW 切到 unit 1/DMA2D R2M。
7. radius=0 耗时多少？热点 R2M 平均 8.8066 ms/frame。
8. decomposition 用了几个 primitive？未执行；收益门槛未达到。
9. 大矩形是否走 DMA2D R2M？未创建 decomposition 大矩形；radius=0 单体已确认走 R2M。
10. corner 是否仍走 SW？未创建 corner primitives。
11. corner 总面积多少？未进入 decomposition，N/A。
12. corner SW 总耗时多少？未进入 decomposition，N/A。
13. 是否存在明显主体 overdraw？未进入 decomposition，N/A。
14. framebuffer 是否 pixel-perfect？Production 未改变；radius=0 是非等价诊断模式，不作为 production 候选。
15. AA corner 有无差异？radius=0 必然移除圆角/AA；因停止门槛触发，未做 decomposition AA diff。
16. SW fill 净减少多少？热点 SW 降为 0；total SW execute 平均减少 9.7312 ms/frame。
17. DMA2D wait 增加多少？平均增加 7.9226 ms/frame。
18. T_render 净下降多少？平均 0.6232 ms/frame。
19. actual presentation 进入几个 panel frames？仍为约 4 panel frames，约 15 FPS。
20. 下一 P0 是什么？本阶段不实施圆角分解；按既有计划回到 opaque card ARGB→XRGB 候选，但本阶段未修改该路径。

## 正确性与故障检查

全部六次 A/B 运行均满足：

- scroll state complete，12/12 steps
- reload request/event/complete：14/14/14
- timeout=0
- ownership violation=0
- LTDC FIFO underrun/transfer/other error=0
- CFSR=0，HFSR=0

## 相关文件

- `Core/Screen/Page/ui_screen_launcher.c`
- `Core/APPS/LVGL/src/draw/dma2d/lv_draw_dma2d.c`
- `Core/APPS/LVGL/src/draw/sw/lv_draw_sw.c`
- `Core/Debug/render_audit.c`
- `tools/gdb/launcher_full_render_audit.gdb`
- `tools/gdb/launcher_rounded_fill_probe.gdb`
- `Docs/display/LAUNCHER_FULL_RENDER_AUDIT.md`
