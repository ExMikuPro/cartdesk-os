# SDRAM 固定内存布局

外部 SDRAM 为 STM32H743 的 FMC Bank2，物理范围 `0xD0000000` ~ `0xD3FFFFFF`，
容量 64 MiB，end-exclusive 结束地址 `0xD4000000`。

**唯一来源**：`Core/Inc/sdram_layout.h`。本文档只是说明，所有数值以该 header 为准；
任何其它文件（linker script、driver、LVGL port、脚本）都不得自行维护绝对地址。

关联文件：

- `Core/Inc/sdram_layout.h` — 布局宏 + 编译期断言（本文档描述的对象）
- `Core/Memory/xhgc_memory_layout.{h,c}` — zone 元数据表（纯引用上面的宏）
- `Core/Screen/Page/launcher_strip.h` — Launcher strip 访问器与地址运算
- `STM32H743XX_FLASH.ld` — MEMORY 区域同步
- `tests/host/sdram_layout_test.c` — host 布局回归测试

## 1. 完整 64 MiB map

end-exclusive 表示法。

| Region | Start | End-exclusive | Size | 对齐 | 说明 |
|---|---:|---:|---:|---:|---|
| Layer 0 Static FB | `0xD0000000` | `0xD0177000` | `0x00177000` (1.46 MiB) | 256 B | Launcher 固定 UI，ARGB8888 800×480 单缓冲 |
| LVGL FB_A | `0xD0177000` | `0xD02EE000` | `0x00177000` | 256 B | Lua Cart / LVGL DIRECT 双缓冲 A |
| LVGL FB_B | `0xD02EE000` | `0xD0465000` | `0x00177000` | 256 B | Lua Cart / LVGL DIRECT 双缓冲 B |
| Launcher Strip Arena | `0xD0465000` | `0xD0865000` | `0x00400000` (4 MiB) | 256 B | Launcher horizontal cached strip 预留 |
| └ strip used | `0xD0465000` | `0xD07F38C0` | `0x0038E8C0` (3.557 MiB) | 32 B | 10656 × 350 |
| └ strip margin | `0xD07F38C0` | `0xD0865000` | `0x00071740` (0.443 MiB) | 32 B | 对齐余量，当前不分配 |
| SDRAM LVGL heap | `0xD0865000` | `0xD1865000` | `0x01000000` (16 MiB) | 32 B | 预留/未来用途；当前 LVGL heap 在片内 AXI SRAM |
| DMA pool | `0xD1865000` | `0xD1C65000` | `0x00400000` (4 MiB) | 64 B | `SDRAM_DmaPoolAlloc()` 线性池 |
| Launcher icon cache | `0xD1C65000` | `0xD2065000` | `0x00400000` (4 MiB) | 32 B | `.launcher_cache` section，12×200×200 图标 |
| APP_ARENA_REST | `0xD2065000` | `0xD4000000` | `0x01F9B000` (~31.6 MiB) | 32 B | Lua heap + resource arena + cold pool |
| └ Lua heap | `0xD2065000` | `0xD2265000` | `0x00200000` (2 MiB) | 32 B | `lua_rt` 的 SDRAM heap |
| └ Resource Arena | `0xD2265000` | `0xD3800000` | `0x0159B000` (~21.6 MiB) | 32 B | Resource Manager 独占 |
| └ cold pool | `0xD3800000` | `0xD4000000` | `0x00800000` (8 MiB) | 32 B | 冷元数据 / QFLASH 字库烧写 staging |

合计：`0x04000000` = 64 MiB，与 `SDRAM_TOTAL_SIZE` 一致。

前四块（Layer 0 FB / FB_A / FB_B / Strip Arena）顺序固定，不允许调整；
从 `0xD0865000` 起是必须固定地址的既有 region，按原 size 紧密排列。

## 2. Launcher strip geometry

| 项 | 值 | 说明 |
|---|---:|---|
| 逻辑宽度 | 2660 px | 可见内容宽度 |
| 逻辑高度 | 350 px | |
| 像素格式 | ARGB8888 / XRGB8888 兼容 | 4 Bpp |
| 原始行字节 | 10640 = `0x2990` | 2660 × 4 |
| **物理 stride** | **10656 = `0x29A0`** | 向 32 B 对齐后的行字节 |
| 物理 stride（像素） | 2664 px | 尾部 4 px 为 padding |
| 实际分配 | `0x38E8C0` = 3,729,600 B | 10656 × 350 |
| 预留 arena | `0x00400000` = 4 MiB | |
| 剩余 margin | `0x00071740` = 464,704 B ≈ 0.443 MiB | 仅作对齐/未来余量 |

### 为什么 stride 是 10656 而不是 10640

10640 不是 32 的倍数：`10640 % 32 = 16`。外部 SDRAM 以 32 byte cache line /
DMA2D FIFO 突发为单位访问，行首不对齐会让每一行的 2D 传输跨行边界、影响
DMA2D 与 LTDC 的取数效率，也让 cache clean/invalidate 的范围难以自然对齐。
把 stride 向上对齐到最近的 32 byte 倍数得到 10656。

代价是每行多 4 个像素（16 B），总共多 350 × 16 = 5600 B，相对 4 MiB arena
可以忽略，换来的是行对齐的确定性。

### padding 不是可见内容

stride 尾部 4 px 只是让行对齐的填充，**不属于** Launcher 的可见 surface。
因此 LTDC hardware panning 的最大 `scroll_x` 是

```
LAUNCHER_SCROLL_MAX_X = 2660 - 800 = 1860
```

而不是 `2664 - 800 = 1864`。当 `scroll_x = 1860` 时最右侧可见像素下标为
`1860 + 799 = 2659 < 2660`（`LAUNCHER_SCROLL_MAX_VISIBLE_X`），仍在逻辑宽度内。

## 3. 为什么预留 Launcher strip

Launcher 使用两层 LTDC 组合：

- Layer 0 = 固定 UI（背景、5 个圆按钮、状态栏、divider 等不随滚动变化的内容）
- Layer 1 = 横向 cached strip（2660×350，通过改 CFBAR 做硬件平移）

这样滚动时不需要每帧重绘整条 strip，也不需要把固定 UI 重新画一遍：滚动只改变
Layer 1 的 source 起始地址。strip 必须是一整块连续的物理缓冲（硬件平移要求
单行连续 + 固定 stride），因此提前预留 4 MiB 固定区间，避免后续再挪动布局。

## 4. Launcher / Lua Cart 显示模式关系

两条显示路径共用同一套 SDRAM 布局，但使用不同的 region：

| 模式 | Layer 0 | Layer 1 |
|---|---|---|
| Launcher | Layer 0 Static FB（单缓冲，固定 UI） | Launcher strip arena（单缓存 surface + 硬件平移） |
| Lua Cart | 不单独占用（沿用 Layer 0 Static FB） | LVGL FB_A / FB_B（DIRECT 双缓冲） |

要点：

- **Layer 0 单缓冲**、**Launcher strip 单缓冲**：这两块不需要 double buffer。
- **只有 Lua Cart 的 LVGL DIRECT 路径需要双缓冲**：FB_A 与 FB_B 各 800×480×4，
  与现有 `NoReload` / VBR / `ReloadEvent` / `flush_wait` 行为保持一致。
- 当前 strip 只完成内存预留，LTDC Layer 1 的窗口、pitch 寄存器、CFBAR 平移和
  Launcher/Lua 显示模式切换属于后续任务；本次不修改渲染行为。

## 5. 编译期与运行期检查

编译期（`sdram_layout.h`）：

- 相邻 region `SDRAM_REGION_NO_OVERLAP(a, b)` / `SDRAM_REGION_TIGHTLY_FOLLOWS(a, b)`
- 前三块 framebuffer 的 base/end 冻结基线
- strip stride 必须 32 B 对齐且不小于逻辑行字节
- `LAUNCHER_STRIP_ALLOC_SIZE <= SDRAM_LAUNCHER_STRIP_ARENA_SIZE`
- 全部 framebuffer / strip base 256 B 对齐，DMA pool 64 B 对齐
- 最后一块 region 不超过 `0xD4000000`

运行期：

- `sdram_layout_check()`（`Core/Driver/SDRAM/sdram.c`）在启动时复检边界、紧贴关系与
  对齐，失败直接进入 `Error_Handler()`
- `xhgc_mem_layout_validate()` 复检 zone 表与统一布局一致
- `tests/host/sdram_layout_test.c` 在 host 上做同样检查并打印 layout table

## 6. 重排后的不变项

- 前三块 framebuffer 的绝对地址与重排前相同（`0xD0000000` / `0xD0177000` / `0xD02EE000`）
- `SDRAM_LVGL_HEAP` / `DMA_POOL` / `LAUNCHER_CACHE` 的 **size 不变**，绝对地址整体上移 4 MiB
- `APP_ARENA_REST` 起点从 `0xD1C65000` 上移到 `0xD2065000`，size 从 `0x0239B000`
  缩小到 `0x01F9B000`；Lua heap（2 MiB）与 cold pool（8 MiB）大小不变
- `RESOURCE_ARENA` 的 base 从 `0xD1E65000` 上移到 `0xD2265000`，size 从
  `0x0139B000`（约 19.6 MiB）变为 `0x0159B000`（约 21.6 MiB，见上表）。
  任何依赖 `RESOURCE_ARENA_BASE` 绝对地址的外部工具都需要同步。
  Resource Manager 仍是唯一 owner；Lua heap（2 MiB）与 cold pool（8 MiB）大小不变

## 7. MPU

整个 64 MiB SDRAM 由 `Core/Src/main.c` 的单一 MPU region（`MPU_REGION_NUMBER5`，
base `SDRAM_BASE_ADDR`，size 64 MB）覆盖。本次重排**不新增 MPU region**，也不修改
该 region 的 cacheability / bufferability / shareability。

region 5 在 `Core/Src/main.c:415-421` 只显式改写 `Number` / `BaseAddress` /
`Size` / `AccessPermission` / `DisableExec`，其余字段沿用上一条 region 4
（`0x80000000`, 256 MB）在 `Core/Src/main.c:404-409` 写入的值，即
`MPU_ACCESS_NOT_CACHEABLE` + `MPU_ACCESS_NOT_BUFFERABLE`，shareable 沿用 region 0
的 `MPU_ACCESS_SHAREABLE`。因此 64 MiB SDRAM 当前为 full-access、executable、
non-cacheable / non-bufferable、shareable。

> 代码内存在不一致：`Core/Memory/xhgc_dcache.c`、`Core/Src/dma2d.c`、
> `Core/Src/jpeg.c` 等路径仍按 cacheable 场景执行 DCache clean / invalidate。
> 这是本次重排之前既有的事实，本任务不修改 MPU、不做 cache 性能优化，
> 只记录该差异。

## 8. 相关文档

- `Docs/memory/SDRAM_Layout_Spec.md` — 布局规格（v2.0，对应本次重排）
- `Docs/memory/Memory_Regression_Checklist.md` — 内存回归清单
- `Docs/display/DMA2D_SelfTest.md` — DMA2D 自检
- `Docs/display/LTDC_LVGL_SYNC_AUDIT.md` — LTDC/LVGL 同步审计
