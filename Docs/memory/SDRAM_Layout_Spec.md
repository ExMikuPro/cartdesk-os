# SDRAM 内存布局规范 v2.0（中文）

> 适用范围：本规范描述 STM32H7 系统中外部
> SDRAM（64MiB）的固定分区结构。\
> 目标：保证 LTDC / LVGL / DMA / 资源加载
> 在同一套地址约束下稳定运行，并避免运行期碎片化问题。
>
> v2.0 变更：为 Launcher 固定 UI、Launcher 水平 cached strip 和后续
> LTDC hardware panning 重排布局。前四块固定为
> Layer0_FB / LVGL_FB_A / LVGL_FB_B / LAUNCHER_STRIP，其余固定 region
> 从 `0xD0865000` 起紧密上移。**不保留旧绝对地址兼容**。
>
> 数值唯一来源为 `Core/Inc/sdram_layout.h`；完整说明见
> `Docs/display/SDRAM_LAYOUT.md`。

------------------------------------------------------------------------

## 1. 总览

SDRAM 物理地址范围：

    0xD0000000 ～ 0xD3FFFFFF

总容量：

    64 MiB (0x04000000 bytes)

SDRAM 采用"固定锚点 + 顺序紧贴"的布局策略，分为以下逻辑区：

1.  Layer0_FB（Launcher 固定 UI，单缓冲）
2.  LVGL_FB_A / LVGL_FB_B（LVGL DIRECT 双缓冲）
3.  LAUNCHER_STRIP（Launcher 水平 cached strip 预留）
4.  LVGL_HEAP
5.  DMA_POOL
6.  LAUNCHER_CACHE
7.  APP_ARENA_REST（Lua heap + resource arena + cold pool）

------------------------------------------------------------------------

## 2. 固定分区总览表

以 end-exclusive 结束地址表示。

| 区域 | 起始地址 | 结束(excl) | 容量 (MiB) | 用途 |
|------|------------|------------|------------|--------------|
| LAYER0_FB | 0xD0000000 | 0xD0177000 | 1.46 | Launcher 固定 UI，单缓冲 |
| LVGL_FB_A | 0xD0177000 | 0xD02EE000 | 1.46 | LVGL DIRECT 双缓冲 A |
| LVGL_FB_B | 0xD02EE000 | 0xD0465000 | 1.46 | LVGL DIRECT 双缓冲 B |
| LAUNCHER_STRIP | 0xD0465000 | 0xD0865000 | 4 | Launcher cached strip 预留（实用 3.557） |
| LVGL_HEAP | 0xD0865000 | 0xD1865000 | 16 | 保留/future-use |
| DMA_POOL | 0xD1865000 | 0xD1C65000 | 4 | DMA 专用区 |
| LAUNCHER_CACHE | 0xD1C65000 | 0xD2065000 | 4 | 图标缓存 |
| APP_ARENA_REST | 0xD2065000 | 0xD4000000 | 约 31.6 | Lua heap + 资源区 + cold pool |
| └ LUA_HEAP | 0xD2065000 | 0xD2265000 | 2 | Lua VM SDRAM heap |
| └ RESOURCE_ARENA | 0xD2265000 | 0xD3800000 | 约 21.6 | Resource Manager 独占 |
| └ COLD_POOL | 0xD3800000 | 0xD4000000 | 8 | 冷元数据 / 字库烧写 staging |


------------------------------------------------------------------------

## 3. 布局策略

### 3.1 锚点原则

-   Layer0_FB 必须（MUST）从 SDRAM 起始地址开始。
-   前三块 framebuffer 与 LAUNCHER_STRIP 的顺序固定，不得调整。
-   各固定区为固定大小，不得在运行期扩展。

### 3.2 紧贴原则

-   所有区域必须（MUST）顺序紧贴排列。
-   不允许人工预留空洞。
-   仅允许对齐填充（Alignment Padding）。

### 3.3 对齐规则

-   framebuffer 与 LAUNCHER_STRIP：必须 256 字节对齐（MUST）。
-   LAUNCHER_STRIP 行 stride：必须 32 字节对齐（MUST）。
-   DMA_POOL：必须 64 字节以上对齐。
-   其它区域：建议 32 字节对齐。

### 3.4 编译期检查

`Core/Inc/sdram_layout.h` 用 `_Static_assert` 固定以下不变量：

-   相邻 region 不重叠且首尾相接（`SDRAM_REGION_NO_OVERLAP` /
    `SDRAM_REGION_TIGHTLY_FOLLOWS`）。
-   前三块 framebuffer 的绝对 base 与冻结基线一致。
-   全部 framebuffer / strip base 256 字节对齐，DMA_POOL 64 字节对齐。
-   strip stride 32 字节对齐且不小于逻辑行字节。
-   `LAUNCHER_STRIP_ALLOC_SIZE <= SDRAM_LAUNCHER_STRIP_ARENA_SIZE`。
-   最后一块 region 不超过 `0xD4000000`。

------------------------------------------------------------------------

## 4. FB 区规范

### 4.1 图层结构

-   Layer0（Launcher 固定 UI）：单缓冲，`SDRAM_LAYER0_FB_BASE`。
-   LVGL（Lua Cart DIRECT 路径）：双缓冲，`SDRAM_LVGL_FB_A_BASE` /
    `SDRAM_LVGL_FB_B_BASE`。
-   LAUNCHER_STRIP：单缓存 surface，2660×350，stride 10656，硬件平移用。

### 4.2 显存容量说明

当前配置：

    800 × 480 × 4 bytes
    = 1,536,000 bytes / frame
    = 4.39 MiB total (三帧)

Launcher strip：

    2660 × 4 = 10640 -> stride 对齐 32B -> 10656
    10656 × 350 = 3,729,600 bytes (0x38E8C0)

### 4.3 使用限制

-   FB 区不得作为通用内存使用。
-   不允许 DMA 写入未 cache clean 的区域。
-   不允许 CPU 算法临时缓冲写入显存区。

------------------------------------------------------------------------

## 5. LVGL_HEAP 规范

-   当前固定容量为 16 MiB，地址范围 `0xD0865000` -- `0xD1864FFF`
    （v2.0 从 `0xD0465000` 整体上移 4 MiB，为 LAUNCHER_STRIP 让位）。
-   实机验证发现将 LVGL builtin/TLSF heap 放入本区会引入显示撕裂/不稳定。
-   当前策略：LVGL runtime heap 使用片内 RAM，`SDRAM_LVGL_HEAP` 保留为 reserved/future-use，不作为默认 lv_mem 主池。
-   meminfo 中本区应保持 `total=0x01000000`，`used=0`，并在 dump 文本中标注 `RESERVED/FUTURE_USE`。
-   LVGL 输出 framebuffer 不属于 LVGL runtime heap，LVGL_FB_A/LVGL_FB_B 双缓冲仍使用独立 FB 区。
-   LVGL runtime heap 仅用于 LVGL 元数据和小对象，例如 `lv_obj`、`lv_image`、style、event、label text 和 descriptor 小结构。
-   大图像禁止进入 LVGL 片内 heap；Lua cart 图片、解码后像素资源，以及 Lua UI image 的 copied/cropped/flipped view buffer 应继续使用 APP_ARENA_REST/LAUNCHER_CACHE 等专用区。
-   禁止作为 DMA buffer 使用，DMA buffer 必须来自 DMA_POOL。

------------------------------------------------------------------------

## 6. DMA_POOL 规范

DMA_POOL 仅用于临时 DMA buffer，不作为通用 heap，也不接收 LVGL 对象或 framebuffer allocation。

### 6.1 DMA buffer 分类

固定 DMA 目标（Fixed DMA Target）不需要来自 DMA_POOL，但允许作为 DMA2D / MDMA / LTDC / 外设 DMA 的源或目标，前提是满足 cache 和对齐规则：

-   LAYER0_FB
-   LVGL_FB_A
-   LVGL_FB_B
-   LAUNCHER_STRIP
-   LAUNCHER_CACHE
-   APP_ARENA_REST 中的资源区

临时 DMA buffer（Temporary DMA Buffer）必须来自 DMA_POOL：

-   USB 临时 RX/TX buffer
-   SDMMC / SPI / QSPI 临时 RX/TX buffer
-   AUDIO DMA buffer
-   解码 staging buffer
-   外设 DMA 中转 buffer
-   任何没有固定 zone 所属关系的 DMA 临时内存

注意：固定 DMA 目标不计入 DMA_POOL used；framebuffer fixed reserve 已由 meminfo 初始化统计，LAUNCHER_CACHE / APP_ARENA_REST 作为 DMA 目标时也不额外计入 DMA_POOL。

### 6.2 DMA_POOL allocator

-   `SDRAM_DmaPoolInit()` / `SDRAM_DmaPoolReset()` 管理整个池的生命周期。
-   `SDRAM_DmaPoolAlloc(size, align)` 和 `SDRAM_DmaPoolCalloc(count, size, align)` 使用线性 / bump allocator。
-   不支持单块 free；回收只能通过 reset。
-   DMA_POOL base/size 来自 `Core/Memory/xhgc_memory_layout.c` 中的 `g_xhgc_mem_zones[XHGC_MEM_ZONE_DMA_POOL]`。
-   对齐至少 64 bytes；小于 64 的 align 自动提升，非 2 的幂 align 会向上修正到合法 2 的幂。
-   越界或非法请求返回 `NULL`，不得 HardFault。
-   `SDRAM_DmaPoolContains(ptr, size)` 用于检查逻辑范围是否完整位于 DMA_POOL。
-   `SDRAM_DmaPoolUsed()` 返回当前 bump used；`SDRAM_DmaPoolPeak()` 返回 reset 后仍保留的峰值。

### 6.3 cache 维护规则

-   CPU 写、DMA 读：DMA 开始前 clean DCache。
-   DMA 写、CPU 读：DMA 完成后 invalidate DCache。
-   双向 DMA：开始前 clean，完成后 invalidate。
-   `xhgc_dcache_clean_range()`、`xhgc_dcache_invalidate_range()`、`xhgc_dcache_clean_invalidate_range()` 统一按 Cortex-M7 32-byte cache line 向外对齐覆盖。
-   cache line 对齐覆盖可能触及调用方逻辑范围相邻的同一 cache line 字节；调用方仍以原始 `ptr + size` 作为 DMA 逻辑范围。
-   Debug 下 helper 可提示地址不在 SDRAM、不在 DMA_POOL 且不属于固定 DMA target、或 size 未 32-byte 对齐。

DMA_POOL allocator 与 meminfo 数据流：

```mermaid
flowchart TD
    Caller["Temporary DMA caller"]
    Pool["SDRAM_DmaPoolAlloc"]
    Layout["DMA_POOL zone table"]
    Meminfo["meminfo DMA tag"]
    Cache["xhgc_dcache_* helper"]
    DMA["DMA peripheral"]

    Caller --> Pool
    Pool --> Layout
    Pool --> Meminfo
    Caller --> Cache
    Caller --> DMA
```

说明：调用者先通过 DMA_POOL 获取临时 DMA buffer，再按 DMA 方向调用 cache helper；本阶段只提供统一 helper，不自动改写现有 DMA2D / MDMA / LTDC 调用路径。

------------------------------------------------------------------------

## 7. LAUNCHER_CACHE 规范

-   存放 ARGB8888 解码后像素。
-   设计容量 ≥ 2 MiB。
-   当前预留 4 MiB。

------------------------------------------------------------------------

## 8. APP_ARENA_REST 规范

-   默认提供线性分配模型。
-   支持 reset()。
-   允许上层在本区内实现可释放的专用子分配器。
-   吃剩余全部空间。
-   禁止跨区写入。

Lua cart 图片资源使用 `APP_ARENA_REST` 中的资源区作为 scene 资源 arena：

-   cart 入口脚本加载后，宿主解析 Header、地址表和 INDEX，生成图片资源目录。
-   第一版使用同步懒加载；`ui.image()` 创建时才把 BGRA8888 图片从 DATA 段读入资源区。
-   RESOURCE_ARENA 运行期必须只有一个 owner；默认 owner 固定为 `resource_manager`。
-   `resource_manager` 负责 scene 资源 arena 的 claim、线性分配、scene reset 和资源 handle 失效。
-   `lua_cart_resource_cache` 当前为 legacy/experimental/disabled；默认不得 claim 或直接管理 `RESOURCE_ARENA_BASE`，也不得与 `resource_manager` 同时维护同一段 arena offset/free list。
-   `ui.image()` 需要生成 copied/cropped/flipped view buffer 时，必须从 APP_ARENA_REST 的资源区或基于该区的 image scratch 分配，不得使用 `lv_malloc()` / LVGL runtime heap。
-   同一个 Drawable 频繁 rebuild view 时应优先复用已有 scratch buffer；容量不足时才追加申请，旧块随 scene reset 统一回收。
-   cart 脚本运行期间，该资源管理器独占资源区；其它代码不得同时通过线性 arena 接口在资源区分配。
-   第一版不启用 MDMA、LRU、eviction、异步加载、压缩资源或 tile streaming。
-   `ui.image()` Drawable 引用对应资源块，并维护引用计数。
-   Drawable 销毁后释放引用；引用计数归零只标记为未使用，不释放 arena 中间块。
-   场景结束时，宿主按应用 owner 销毁专属 UI 根容器，再统一 reset scene arena 并让旧资源 handle 失效；该过程不依赖 Lua table 内容。

------------------------------------------------------------------------

## 9. 调试定位规则

  地址范围     可能问题来源
  ------------ --------------
  0xD000xxxx   Layer0_FB
  0xD01xxxxx   LVGL_FB_A
  0xD02xxxxx   LVGL_FB_B
  0xD04xxxxx   LAUNCHER_STRIP
  0xD08xxxxx   LVGL_HEAP（reserved）
  0xD18xxxxx   DMA_POOL
  0xD1Cxxxxx   LAUNCHER_CACHE
  0xD20xxxxx   APP_ARENA_REST（Lua heap / resource arena）
  0xD38xxxxx   COLD_POOL / Arena 溢出

------------------------------------------------------------------------

## 10. Meminfo 统计

`Core/Memory/xhgc_meminfo.h` 提供 SDRAM zone 和 memory tag 的运行期统计骨架。

-   `xhgc_meminfo_init()` 必须在 `xhgc_mem_layout_validate()` 通过后调用。
-   初始化时从 `g_xhgc_mem_zones` 读取每个 zone 的 `total`。
-   三块 framebuffer zone 初始化为 fixed reserved，tag 为 `FRAMEBUFFER`，总占用 `0x00465000`（3 × `0x177000`）。
-   LAUNCHER_STRIP zone 按实际 strip 用量 `0x0038E8C0` 记为 reserved/used（tag `LAUNCHER`），4 MiB arena 的剩余 margin 保持空闲；启动期不 memset 整块 arena。
-   fixed framebuffer 不允许通过 `xhgc_meminfo_release()` 释放。
-   `xhgc_meminfo_alloc_record()` / `xhgc_meminfo_free_record()` 只记录已发生的分配和释放。
-   `xhgc_meminfo_fail_record()` 只记录失败次数。
-   meminfo 不分配内存，不替换 `malloc/free`，不接管 LVGL、DMA、Lua、newlib 或 FreeRTOS heap。
-   LVGL runtime heap 当前位于片内 RAM，不计入任何 SDRAM zone；`SDRAM_LVGL_HEAP` 作为 reserved/future-use 区域显示，`used` 保持 0。
-   DMA_POOL 分配成功会记录 `XHGC_MEM_ZONE_DMA_POOL` + `XHGC_MEM_TAG_DMA` 的 used、peak 和 alloc_count；记录大小为 bump allocator 实际消耗空间，包含对齐 padding。
-   DMA_POOL 分配失败会记录 `XHGC_MEM_ZONE_DMA_POOL` + `XHGC_MEM_TAG_DMA` 的 fail_count。
-   DMA_POOL reset 会将 `XHGC_MEM_ZONE_DMA_POOL` used 归零或回到基线；peak 和 fail_count 保留。
-   固定 DMA 目标不进入 DMA_POOL meminfo used，避免 framebuffer reserve、LAUNCHER_CACHE 或 APP_ARENA_REST 资源重复计数。
-   APP_ARENA_REST 第一阶段 meminfo 统计以总 zone 为单位，`app_arena_alloc()` 成功、失败和 reset 会同步该 zone 的 used、peak 和 fail；Lua UI image view buffer 申请成功会增加 APP_ARENA_REST used/peak，申请失败会增加 fail_count。
-   RESOURCE_ARENA owner guard 不改变 meminfo 模型；统计仍以 APP_ARENA_REST 总 zone + `RESOURCE`/`TEXTURE` tag 为准。
-   大图像 view buffer 不应增加 `LVGL` tag；当前通过资源区接口分配，tag 计入 `RESOURCE`。
-   Lua VM heap 位于 APP_ARENA_REST 内的 `LUA_HEAP` 子区；当前 meminfo 仍按 APP_ARENA_REST 总 zone 统计，并以 `XHGC_MEM_TAG_LUA` 记录 `lua_vm_alloc()` 的成功、释放和失败。
-   RESOURCE_ARENA、LUA_HEAP、COLD_POOL 等 APP_ARENA_REST 内部子区级统计留到后续阶段，不在本阶段重排地址或改变子区模型。
-   Debug 构建可通过 CMake 选项 `XHGC_MEMINFO_SELFTEST_ENABLE=ON` 打开 APP_ARENA_REST meminfo 自测；默认关闭。
-   Debug 构建可通过 CMake 选项 `XHGC_DMA_POOL_SELFTEST_ENABLE=ON` 打开 DMA_POOL meminfo 自测；默认关闭，默认构建不包含 selftest 符号。

### 10.1 Lua VM allocator 约束

-   固件业务代码必须通过 `lua_vm_newstate()` 创建主 `lua_State`。
-   `lua_vm_newstate()` 内部使用 `lua_newstate(lua_vm_alloc, lua_vm_memory_allocator())`，不得回退到 `luaL_newstate()` 或 newlib heap。
-   `luaL_newstate()` 和 Lua 默认 `l_alloc` 只允许作为 `Core/LuaPort/src` 中 Lua 官方源码实现存在，不作为固件主路径使用。
-   业务源码不得直接调用 `luaL_newstate()`；Debug 构建会运行 `cmake/check_lua_allocator_usage.cmake` 检查固件业务源码中的误用。
-   `lua_vm_alloc()` OOM 时返回 `NULL`，并通过 meminfo 记录 `APP_ARENA_REST` + `XHGC_MEM_TAG_LUA` 的失败次数；Lua VM 创建失败时由运行时输出明确日志。

### 10.2 残余 allocator 管控

-   详细 policy 见 `Docs/memory/Allocator_Policy.md`。
-   固件业务源码不得直接调用 `malloc/free/calloc/realloc`；newlib `_sbrk` 保留为 C 库 fallback 后端。
-   Debug 构建会运行 `cmake/check_allocator_usage.cmake`，扫描业务源码中的直接 newlib allocator 调用并输出文件路径和行号。
-   FreeRTOS `heap_4` 保留，仅用于 RTOS 对象、任务、队列、timer 和同步原语，不用于游戏资源、图片、Lua、LVGL 大对象或 DMA buffer。
-   littlefs 正常路径通过 `Core/Driver/FLASH/lfs_port.c` 提供 cold pool read/prog/lookahead buffer；`lfs_malloc` / `lfs_free` fallback 保留并通过 `lfs_malloc_fallback_count` / `lfs_free_fallback_count` 计数。
-   `RNG_Shuffle()` 不再使用 newlib malloc；小元素使用 64 bytes 栈 scratch，大元素必须通过 `RNG_ShuffleWithScratch()` 由调用方显式提供 scratch buffer。

启动串口日志会先输出 `[XHGC SDRAM LAYOUT]`，再输出 `[XHGC MEMINFO]`。
自测启用时，日志会额外输出 `[XHGC MEMINFO SELFTEST] baseline`、`after_alloc`、`after_reset` 和 PASS/FAIL。

------------------------------------------------------------------------

## 版本记录

-   v2.0 为 Launcher 固定 UI / Launcher 水平 cached strip / 后续 LTDC hardware
    panning 重排 SDRAM：固定前四块为 LAYER0_FB / LVGL_FB_A / LVGL_FB_B /
    LAUNCHER_STRIP（4 MiB），LVGL_HEAP / DMA_POOL / LAUNCHER_CACHE / APP_ARENA_REST
    整体上移 4 MiB，RESOURCE_ARENA 紧随 Lua heap 并保留 cold pool 8 MiB；
    新增 `_Static_assert` 布局断言与 `LAUNCHER_STRIP_*` 几何宏；
    不再保留旧绝对地址兼容
-   v1.0 回退 LVGL runtime heap 到片内 RAM，SDRAM_LVGL_HEAP 保留为 reserved/future-use
-   v1.0 添加 Phase 9 allocator policy 入口、newlib 检查、littlefs fallback 计数和 RNG scratch 规则
-   v1.0 补充 DMA_POOL 临时 DMA buffer 规则、cache helper 和 meminfo 接入
-   v1.0 添加 meminfo 统计骨架说明
-   v1.0 初始发布版本
