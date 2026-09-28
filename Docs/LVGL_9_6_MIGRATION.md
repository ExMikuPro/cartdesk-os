# LVGL 9.5.0 → 9.6.0 迁移记录

## 版本与范围

- 旧上游：`v9.5.0`，`85aa60d18b3d5e5588d7b247abf90198f07c8a63`
- 新上游：`v9.6.0`，`80ca777e37a2b176770726a02e07a6fb79ef0b39`
- 迁移范围：`Core/APPS/LVGL` 的上游源码、demo/example 快照及新增的 `include/lvgl/**` public header tree。
- 未修改：Resource Pipeline V2、XIMG/XHGCIDX2、JPEG binary contract、SDRAM/FMC/LTDC/CubeMX、FreeRTOS task 架构、Lua API 与 framebuffer 数量。

## Local Delta Report

对本地 HEAD 的 vendored tree 与上游 v9.5.0 作逐文件 blob 比较：

| 分类 | 数量 | 处理 |
| --- | ---: | --- |
| 与上游 v9.5 完全相同 | 1727 | 用对应 v9.6 文件替换 |
| CartDesk 集成文件 | 14 | 保留 `font/**`、`port/**` 与 `lv_conf.h` |
| 上游文件上的本地 patch | 6 | 见下表 |

| 原本地差异 | v9.6 决策 |
| --- | --- |
| `src/draw/dma2d/lv_draw_dma2d.c` | 有意不移植。v9.6 已接管 draw-buffer cache handler 注册、绘制前 flush、完成后 invalidate、地址对齐告警及同步 dispatch 流程；旧 patch 的逐行 cache 维护和 image draw-unit 禁用会覆盖这些改进。 |
| `src/osal/lv_cmsis_rtos2.c` | 保留最小差异：`lv_mutex_lock` 使用 `osWaitForever`，以保持 CartDesk 跨 FreeRTOS task 的阻塞互斥语义。 |
| `src/libs/gltf/gltf_data/*`、`src/libs/nanovg/LICENSE.txt` | 仅换行/许可证快照差异；采用 v9.6 上游版本。 |

## 配置与 API 迁移

- `LV_COLOR_DEPTH=32` 已移除，改为 `LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_ARGB8888`。
- 实际 display format 仍由 `Core/APPS/LVGL/port/lv_port_disp.c` 显式设为 `LV_COLOR_FORMAT_ARGB8888`；LTDC 两层仍为 `LTDC_PIXEL_FORMAT_ARGB8888`（`Core/Src/ltdc.c`）。双 framebuffer、DIRECT render mode 与 straight-alpha Resource Pipeline 契约未变。
- 未使用旧 `LV_DRAW_THREAD_STACKSIZE`。内存、assert、CMSIS-RTOS2、DMA2D 与字体配置均按 v9.6 template 复核后保留原语义。
- CartDesk 代码已清除 9.6 标记为 deprecated 的 `lv_obj_add_flag` / `lv_obj_remove_flag` 和从 `src/tick` 引入 public header 的用法，改用专用 public setters 与 `lvgl.h`。
- `lvgl_init.c` 不再包含 draw-buffer private header 或覆盖 cache handlers。唯一确认的 CartDesk private-API 依赖位于 `Core/Src/stm32h7xx_it.c`：它为 DMA2D IRQ 调用 `lv_draw_dma2d_transfer_complete_interrupt_handler()`，该函数在 v9.6 没有 public header。该依赖保持不变并应在未来上游公开 IRQ bridge 后移除；其余 `port/`、`TASK/`、`LuaPort/`、`Screen/`、`Driver/`、`Memory/` 不依赖 LVGL private API。

## Source Inventory 与一致性

- v9.5 `src/**/*.c`：463 个；v9.6：483 个；当前 vendor：483 个。
- `Core/APPS/LVGL/src` 和 `include` 与 v9.6 snapshot 完全一致，唯一例外是已记录的 `src/osal/lv_cmsis_rtos2.c` 最小 CartDesk patch。
- `lv_version.h` 的实际版本宏为 `9.6.0`。新的 `include/lvgl/**` 已加入 `LVGL_PUBLIC_INCLUDES`。
- `include/lvgl/api_map/lv_api_map_v9_5.h` 与 `examples/xml_project/project.xml` 中的 `9.5` 文本来自 v9.6 upstream snapshot 的兼容/API 示例内容，不是 CartDesk 混入的 9.5 core source 或 public header。
- Debug build 确认编译 `lv_draw_dma2d.c`、`lv_draw_dma2d_fill.c`、`lv_draw_dma2d_img.c`，且 `LV_USE_DRAW_DMA2D=1` 保持启用。

## 验证结果

| 项目 | 9.6 结果 |
| --- | --- |
| HostTest | PASS：15/15 |
| Debug | PASS |
| Release | PASS |
| SizeDebug | PASS |
| Debug-USB-SD-MSC | PASS |
| SizeDebug-DMA2D-SelfTest | PASS（构建） |
| DMA2D L1 R2M | TARGET VALIDATION PENDING |
| DMA2D L2 M2M tight | TARGET VALIDATION PENDING |
| DMA2D L3 M2M strided | TARGET VALIDATION PENDING |
| DMA2D L4 RGB565→ARGB8888 PFC | TARGET VALIDATION PENDING |
| Launcher / touch / scroll / QFlash font | TARGET VALIDATION PENDING |
| Cart Lua UI / READY image / JPEG image | TARGET VALIDATION PENDING |
| HardFault / MemManage / BusFault / UsageFault | TARGET VALIDATION PENDING |

本次环境未连接可执行刷写和观测的目标板，因此不能把编译或 HostTest 结果代替 L1–L4、Launcher 或 Cart 真机 smoke。LVGL 与 Resource Pipeline 共用 DMA2D 的既有 ownership 边界也未重构；v9.6 未引入新的 arbiter，真机验证应继续关注并发资源转换与 LVGL 绘制。v9.6 display synchronization API 值得后续单独评估用于 LTDC/VSync，但本次未改动现有 swap 逻辑。

## 结论

**TARGET VALIDATION PENDING**。构建与 HostTest 基线已经建立；完成 L1–L4 和 Launcher/Cart 真机 smoke 且故障计数为零后，才能标记为 `READY TO MERGE`。
