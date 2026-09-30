#ifndef SDRAM_LAYOUT_H
#define SDRAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * SDRAM 统一固定布局
 * ----------------------------------------------------------------------------
 * 物理范围 : 0xD0000000 ~ 0xD3FFFFFF (64 MiB)
 * 结束地址 : 0xD4000000 (end-exclusive)
 *
 * 本文件是 production SDRAM 布局的唯一来源 (single source of truth)。
 * 其它 header / driver / linker script 只能引用这里的符号，禁止再散落
 * 绝对地址或重复的魔法数字。
 *
 * 前四块顺序固定，不允许调整：
 *
 *   0xD0000000 -> 0xD0177000  Layer 0 static framebuffer (Launcher 固定 UI)
 *   0xD0177000 -> 0xD02EE000  LVGL framebuffer A
 *   0xD02EE000 -> 0xD0465000  LVGL framebuffer B
 *   0xD0465000 -> 0xD0865000  Launcher strip arena (正式预留 4 MiB)
 *
 * 其后为必须固定地址的既有 region，从 0xD0865000 开始紧密排列：
 *
 *   0xD0865000 -> 0xD0C65000  LVGL / 大块资源固定堆
 *   0xD0C65000 -> 0xD1065000  DMA pool
 *   0xD1065000 -> 0xD1465000  Launcher icon cache
 *   0xD1465000 -> 0xD4000000  APP_ARENA_REST
 *
 * APP_ARENA_REST 内部分为：Lua heap / resource arena / cold pool。
 * Resource Manager 是 RESOURCE_ARENA 的唯一 owner。
 *
 * 详细说明见 Docs/display/SDRAM_LAYOUT.md。
 * ========================================================================== */

/* ---------------------------------------------------------------- SDRAM 边界 */

#define SDRAM_BASE_ADDR               ((uintptr_t)0xD0000000UL)
#define SDRAM_TOTAL_SIZE              ((uint32_t)0x04000000UL)
#define SDRAM_END_ADDR                ((uintptr_t)0xD3FFFFFFUL)
#define SDRAM_LIMIT_ADDR              ((uintptr_t)(SDRAM_BASE_ADDR + (uintptr_t)SDRAM_TOTAL_SIZE))

/* 兼容别名：统一布局下 base/size/end 与既有命名保持一致 */
#define SDRAM_BASE                    SDRAM_BASE_ADDR
#define SDRAM_SIZE                    SDRAM_TOTAL_SIZE
#define SDRAM_END                     SDRAM_END_ADDR
#define SDRAM_END_EXCLUSIVE           SDRAM_LIMIT_ADDR

/* ------------------------------------------------- Region 0: Layer 0 static FB
 * Launcher 固定 UI（背景 / 5 个圆按钮 / 状态栏 / divider 等不随 strip 滚动的
 * 内容），ARGB8888 800x480 单缓冲。
 */
#define SDRAM_LAYER0_FB_WIDTH         ((uint32_t)800UL)
#define SDRAM_LAYER0_FB_HEIGHT        ((uint32_t)480UL)
#define SDRAM_LAYER0_FB_BPP           ((uint32_t)4UL)
#define SDRAM_LAYER0_FB_BASE          ((uintptr_t)(SDRAM_BASE_ADDR))
#define SDRAM_LAYER0_FB_SIZE          ((uint32_t)(SDRAM_LAYER0_FB_WIDTH * SDRAM_LAYER0_FB_HEIGHT * SDRAM_LAYER0_FB_BPP))
#define SDRAM_LAYER0_FB_END           ((uintptr_t)(SDRAM_LAYER0_FB_BASE + (uintptr_t)SDRAM_LAYER0_FB_SIZE))

/* ------------------------------------------------------ Region 1: LVGL FB_A
 * Lua Cart / 普通 LVGL DIRECT 双缓冲的 A 面。
 */
#define SDRAM_LVGL_FB_WIDTH           SDRAM_LAYER0_FB_WIDTH
#define SDRAM_LVGL_FB_HEIGHT          SDRAM_LAYER0_FB_HEIGHT
#define SDRAM_LVGL_FB_BPP             SDRAM_LAYER0_FB_BPP
#define SDRAM_LVGL_FB_SIZE            SDRAM_LAYER0_FB_SIZE
#define SDRAM_LVGL_FB_A_BASE          ((uintptr_t)(SDRAM_LAYER0_FB_END))
#define SDRAM_LVGL_FB_A_SIZE          SDRAM_LVGL_FB_SIZE
#define SDRAM_LVGL_FB_A_END           ((uintptr_t)(SDRAM_LVGL_FB_A_BASE + (uintptr_t)SDRAM_LVGL_FB_A_SIZE))

/* ------------------------------------------------------ Region 2: LVGL FB_B */
#define SDRAM_LVGL_FB_B_BASE          ((uintptr_t)(SDRAM_LVGL_FB_A_END))
#define SDRAM_LVGL_FB_B_SIZE          SDRAM_LVGL_FB_SIZE
#define SDRAM_LVGL_FB_B_END           ((uintptr_t)(SDRAM_LVGL_FB_B_BASE + (uintptr_t)SDRAM_LVGL_FB_B_SIZE))

/* ------------------------------------------ Region 3: Launcher strip arena
 * Launcher App horizontal cached strip 专用预留区。
 * 逻辑 surface 2660x350 ARGB8888/XRGB8888，stride 向 32 byte 对齐。
 * 详细几何定义见下方 Launcher strip 宏。
 */
#define SDRAM_LAUNCHER_STRIP_BASE          ((uintptr_t)(SDRAM_LVGL_FB_B_END))
#define SDRAM_LAUNCHER_STRIP_ARENA_SIZE    ((uint32_t)0x00400000UL)
#define SDRAM_LAUNCHER_STRIP_ARENA_END     ((uintptr_t)(SDRAM_LAUNCHER_STRIP_BASE + (uintptr_t)SDRAM_LAUNCHER_STRIP_ARENA_SIZE))

/* ============================================================ Launcher strip
 * 逻辑尺寸 2660x350，ARGB8888/XRGB8888 兼容，4 Bpp。
 * 原始行字节 2660*4 = 10640 (0x2990)，但对 DMA2D / cache line / LTDC 需要
 * 32 byte 对齐，因此物理 stride = 10656 (0x29A0) = 2664 pixels。
 * stride 尾部 4 个 padding 像素不属于可见内容，LTDC 平移最大 x 仍是 1860，
 * 而不是 2664-800。
 * ========================================================================== */

#define LAUNCHER_STRIP_WIDTH               ((uint32_t)2660UL)
#define LAUNCHER_STRIP_HEIGHT              ((uint32_t)350UL)
#define LAUNCHER_STRIP_BPP                 ((uint32_t)4UL)
#define LAUNCHER_STRIP_ROW_BYTES           ((uint32_t)(LAUNCHER_STRIP_WIDTH * LAUNCHER_STRIP_BPP))
#define LAUNCHER_STRIP_ALIGN_BYTES         ((uint32_t)SDRAM_DEFAULT_ALIGN)

#define SDRAM_ALIGN_UP_U32(value, align) \
    (((uint32_t)(value) + ((uint32_t)(align) - 1UL)) & ~((uint32_t)(align) - 1UL))

#define LAUNCHER_STRIP_STRIDE_BYTES        SDRAM_ALIGN_UP_U32(LAUNCHER_STRIP_ROW_BYTES, LAUNCHER_STRIP_ALIGN_BYTES)
#define LAUNCHER_STRIP_STRIDE_PIXELS       ((uint32_t)(LAUNCHER_STRIP_STRIDE_BYTES / LAUNCHER_STRIP_BPP))
#define LAUNCHER_STRIP_ALLOC_SIZE          ((uint32_t)(LAUNCHER_STRIP_STRIDE_BYTES * LAUNCHER_STRIP_HEIGHT))
#define LAUNCHER_STRIP_ARENA_MARGIN_BYTES  ((uint32_t)(SDRAM_LAUNCHER_STRIP_ARENA_SIZE - LAUNCHER_STRIP_ALLOC_SIZE))

#define LAUNCHER_VIEWPORT_WIDTH            ((uint32_t)800UL)
#define LAUNCHER_VIEWPORT_HEIGHT           ((uint32_t)350UL)

/* 最大水平滚动量：2660 - 800 = 1860 */
#define LAUNCHER_SCROLL_MAX_X              ((uint32_t)(LAUNCHER_STRIP_WIDTH - LAUNCHER_VIEWPORT_WIDTH))
/* scroll_x = 1860 时最右侧可见像素下标：1860 + 799 = 2659 < 2660 */
#define LAUNCHER_SCROLL_MAX_VISIBLE_X      ((uint32_t)(LAUNCHER_SCROLL_MAX_X + LAUNCHER_VIEWPORT_WIDTH - 1UL))

/* --------------------------------------------- 既有固定 region（strip 之后）
 * size 保持与重排前一致，只改变绝对地址。
 */
#define SDRAM_LVGL_HEAP_BASE          ((uintptr_t)(SDRAM_LAUNCHER_STRIP_ARENA_END))
#define SDRAM_LVGL_HEAP_SIZE          ((uint32_t)0x01000000UL)
#define SDRAM_LVGL_HEAP_END           ((uintptr_t)(SDRAM_LVGL_HEAP_BASE + (uintptr_t)SDRAM_LVGL_HEAP_SIZE))

#define SDRAM_DMA_POOL_BASE           ((uintptr_t)(SDRAM_LVGL_HEAP_END))
#define SDRAM_DMA_POOL_SIZE           ((uint32_t)0x00400000UL)
#define SDRAM_DMA_POOL_END            ((uintptr_t)(SDRAM_DMA_POOL_BASE + (uintptr_t)SDRAM_DMA_POOL_SIZE))

#define SDRAM_LAUNCHER_CACHE_BASE     ((uintptr_t)(SDRAM_DMA_POOL_END))
#define SDRAM_LAUNCHER_CACHE_SIZE     ((uint32_t)0x00400000UL)
#define SDRAM_LAUNCHER_CACHE_END      ((uintptr_t)(SDRAM_LAUNCHER_CACHE_BASE + (uintptr_t)SDRAM_LAUNCHER_CACHE_SIZE))

/* ------------------------------------------------- APP_ARENA_REST 及内部划分 */
#define SDRAM_APP_ARENA_BASE          ((uintptr_t)(SDRAM_LAUNCHER_CACHE_END))
#define SDRAM_APP_ARENA_END           ((uintptr_t)SDRAM_END_ADDR)
#define SDRAM_APP_ARENA_SIZE          ((uint32_t)(SDRAM_APP_ARENA_END - SDRAM_APP_ARENA_BASE + 1UL))

/*
 * APP_ARENA_REST split:
 *   - Lua heap 占低端 2 MiB，不被 resource arena API 重置
 *   - resource arena 紧贴 Lua heap 向上延伸
 *   - cold pool 预留高端 8 MiB 存放冷元数据 / 缓存对象
 */
#define LUA_HEAP_SIZE                 ((uint32_t)0x00200000UL)
#define LUA_HEAP_BASE                 SDRAM_APP_ARENA_BASE
#define LUA_HEAP_END                  ((uintptr_t)(LUA_HEAP_BASE + (uintptr_t)LUA_HEAP_SIZE - 1UL))

#define COLD_POOL_SIZE                ((uint32_t)0x00800000UL)
#define COLD_POOL_END                 SDRAM_APP_ARENA_END
#define COLD_POOL_BASE                ((uintptr_t)(COLD_POOL_END + 1UL - (uintptr_t)COLD_POOL_SIZE))

#define RESOURCE_ARENA_BASE           ((uintptr_t)(LUA_HEAP_END + 1UL))
#define RESOURCE_ARENA_END            ((uintptr_t)(COLD_POOL_BASE - 1UL))
#define RESOURCE_ARENA_SIZE           ((uint32_t)(RESOURCE_ARENA_END - RESOURCE_ARENA_BASE + 1UL))

/* 与重排前一致的绝对地址（冻结基线，仅用于布局回归；不得用于新代码） */
#define SDRAM_LAYOUT_FROZEN_LAYER0_FB_BASE      ((uintptr_t)0xD0000000UL)
#define SDRAM_LAYOUT_FROZEN_LVGL_FB_A_BASE      ((uintptr_t)0xD0177000UL)
#define SDRAM_LAYOUT_FROZEN_LVGL_FB_B_BASE      ((uintptr_t)0xD02EE000UL)

/* --------------------------------------------------------------- 对齐常量 */
#define SDRAM_FB_ALIGN                ((uint32_t)256UL)
#define SDRAM_DMA_ALIGN               ((uint32_t)64UL)
#define SDRAM_DEFAULT_ALIGN           ((uint32_t)32UL)
#define SDRAM_LAYOUT_ALIGN            SDRAM_DEFAULT_ALIGN

/* ------------------------------------------------------------ 地址判定工具 */

static inline uintptr_t sdram_align_up_uintptr(uintptr_t value, size_t align)
{
    uintptr_t mask = (uintptr_t)align - 1u;
    return (value + mask) & ~mask;
}

static inline int sdram_addr_in_range(uintptr_t addr, uintptr_t base, uintptr_t end)
{
    return (addr >= base) && (addr <= end);
}

static inline int sdram_addr_in_fb(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END - 1UL) ||
           sdram_addr_in_range(addr, SDRAM_LVGL_FB_A_BASE, SDRAM_LVGL_FB_A_END - 1UL) ||
           sdram_addr_in_range(addr, SDRAM_LVGL_FB_B_BASE, SDRAM_LVGL_FB_B_END - 1UL);
}

static inline int sdram_addr_in_layer0_fb(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END - 1UL);
}

static inline int sdram_addr_in_launcher_strip(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAUNCHER_STRIP_BASE,
                               SDRAM_LAUNCHER_STRIP_ARENA_END - 1UL);
}

static inline int sdram_addr_in_lvgl_heap(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END - 1UL);
}

static inline int sdram_addr_in_dma_pool(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END - 1UL);
}

static inline int sdram_addr_in_launcher_cache(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END - 1UL);
}

static inline int sdram_addr_in_app_arena(uintptr_t addr)
{
    return sdram_addr_in_range(addr, SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END);
}

static inline int sdram_addr_in_lua_heap(uintptr_t addr)
{
    return sdram_addr_in_range(addr, LUA_HEAP_BASE, LUA_HEAP_END);
}

static inline int sdram_addr_in_resource_arena(uintptr_t addr)
{
    return sdram_addr_in_range(addr, RESOURCE_ARENA_BASE, RESOURCE_ARENA_END);
}

static inline int sdram_addr_in_cold_pool(uintptr_t addr)
{
    return sdram_addr_in_range(addr, COLD_POOL_BASE, COLD_POOL_END);
}

/* ============================================================================
 * 运行期布局自检（编译期断言的运行时补充）
 *
 * 语义约定统一为 end-exclusive；只有当配置了
 * SDRAM_LAYOUT_FAIL_HOOK() 时才会在失败点触发回调。
 * 设备侧由 Core/Driver/SDRAM/sdram.c 绑定到 Error_Handler()，
 * host 测试可以直接调用并读取返回值。
 * ========================================================================== */

static inline int sdram_layout_tightly_follows(uintptr_t prev_end_exclusive,
                                               uintptr_t next_base)
{
    return prev_end_exclusive == next_base;
}

static inline int sdram_layout_range_aligned(uintptr_t base,
                                             uintptr_t end_exclusive,
                                             size_t align)
{
    return align != 0u && (base % align) == 0u && (end_exclusive % align) == 0u;
}

/**
 * @brief  复检统一布局的边界、紧贴关系与对齐要求
 * @retval 0=全部通过, 非0=首个失败检查的编号
 * @note   与 sdram_layout.h 的编译期断言检查同一组不变量；设备侧在启动时调用，
 *         host 侧由 tests/host/sdram_layout_test.c 调用
 */
static inline int sdram_layout_runtime_validate(void)
{
    int failure = 0;

    if (SDRAM_BASE_ADDR != SDRAM_LAYER0_FB_BASE ||
        SDRAM_END_ADDR != SDRAM_APP_ARENA_END ||
        SDRAM_TOTAL_SIZE != (uint32_t)(SDRAM_END_ADDR - SDRAM_BASE_ADDR + 1UL)) {
        failure = 1;
    } else if (!sdram_layout_tightly_follows(SDRAM_LAYER0_FB_END, SDRAM_LVGL_FB_A_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_LVGL_FB_A_END, SDRAM_LVGL_FB_B_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_LVGL_FB_B_END, SDRAM_LAUNCHER_STRIP_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LVGL_HEAP_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_LVGL_HEAP_END, SDRAM_DMA_POOL_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_DMA_POOL_END, SDRAM_LAUNCHER_CACHE_BASE) ||
               !sdram_layout_tightly_follows(SDRAM_LAUNCHER_CACHE_END, SDRAM_APP_ARENA_BASE)) {
        failure = 2;
    } else if (!sdram_layout_range_aligned(SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END, SDRAM_FB_ALIGN) ||
               !sdram_layout_range_aligned(SDRAM_LVGL_FB_A_BASE, SDRAM_LVGL_FB_A_END, SDRAM_FB_ALIGN) ||
               !sdram_layout_range_aligned(SDRAM_LVGL_FB_B_BASE, SDRAM_LVGL_FB_B_END, SDRAM_FB_ALIGN) ||
               !sdram_layout_range_aligned(SDRAM_LAUNCHER_STRIP_BASE, SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_FB_ALIGN)) {
        failure = 3;
    } else if (!sdram_layout_range_aligned(SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END, SDRAM_DMA_ALIGN)) {
        failure = 4;
    } else if (!sdram_layout_range_aligned(SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END, SDRAM_DEFAULT_ALIGN) ||
               !sdram_layout_range_aligned(SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END, SDRAM_DEFAULT_ALIGN) ||
               !sdram_layout_range_aligned(SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END + 1UL, SDRAM_DEFAULT_ALIGN) ||
               !sdram_layout_range_aligned(LUA_HEAP_BASE, LUA_HEAP_END + 1UL, SDRAM_DEFAULT_ALIGN) ||
               !sdram_layout_range_aligned(RESOURCE_ARENA_BASE, RESOURCE_ARENA_END + 1UL, SDRAM_DEFAULT_ALIGN) ||
               !sdram_layout_range_aligned(COLD_POOL_BASE, COLD_POOL_END + 1UL, SDRAM_DEFAULT_ALIGN)) {
        failure = 5;
    } else if (LAUNCHER_STRIP_STRIDE_BYTES < LAUNCHER_STRIP_ROW_BYTES ||
               (LAUNCHER_STRIP_STRIDE_BYTES % SDRAM_DEFAULT_ALIGN) != 0UL ||
               LAUNCHER_STRIP_ALLOC_SIZE > SDRAM_LAUNCHER_STRIP_ARENA_SIZE ||
               LAUNCHER_SCROLL_MAX_VISIBLE_X >= LAUNCHER_STRIP_WIDTH) {
        failure = 6;
    } else if (!sdram_addr_in_app_arena(LUA_HEAP_BASE) ||
               !sdram_addr_in_app_arena(LUA_HEAP_END) ||
               !sdram_addr_in_app_arena(RESOURCE_ARENA_BASE) ||
               !sdram_addr_in_app_arena(RESOURCE_ARENA_END) ||
               !sdram_addr_in_app_arena(COLD_POOL_BASE) ||
               !sdram_addr_in_app_arena(COLD_POOL_END) ||
               !sdram_layout_tightly_follows(LUA_HEAP_END + 1UL, RESOURCE_ARENA_BASE) ||
               RESOURCE_ARENA_END >= COLD_POOL_BASE) {
        failure = 7;
    }

#ifdef SDRAM_LAYOUT_FAIL_HOOK
    if (failure != 0) {
        SDRAM_LAYOUT_FAIL_HOOK();
    }
#endif

    return failure;
}

/* ============================================================================
 * 编译期布局断言
 * ========================================================================== */

#ifdef __cplusplus
#define SDRAM_STATIC_ASSERT static_assert
#else
#define SDRAM_STATIC_ASSERT _Static_assert
#endif

/* 相邻 region：a 的 end-exclusive 必须 <= b 的 start（等价于不重叠 + 无负间隙） */
#define SDRAM_REGION_NO_OVERLAP(a, b) \
    ((int)((uintptr_t)(a) <= (uintptr_t)(b)))

#define SDRAM_REGION_TIGHTLY_FOLLOWS(a, b) \
    ((int)((uintptr_t)(a) == (uintptr_t)(b)))

#define SDRAM_U32_IS_ALIGNED(value, align) \
    (((uint32_t)(value) & ((uint32_t)(align) - 1UL)) == 0UL)

#define SDRAM_ADDR_IN_RANGE_CHK(addr, base, end) \
    ((int)((uintptr_t)(addr) >= (uintptr_t)(base) && (uintptr_t)(addr) <= (uintptr_t)(end)))

/* --- 边界与总量 --- */
SDRAM_STATIC_ASSERT(SDRAM_LIMIT_ADDR == (uintptr_t)(SDRAM_END_ADDR + 1UL), "SDRAM limit mismatch");
SDRAM_STATIC_ASSERT(SDRAM_TOTAL_SIZE == 0x04000000UL, "SDRAM total size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAYER0_FB_BASE == SDRAM_BASE_ADDR, "Layer0 FB must start at SDRAM base");
SDRAM_STATIC_ASSERT(SDRAM_APP_ARENA_END == SDRAM_END_ADDR, "APP_ARENA_REST must end at SDRAM end");

/* --- 前四块：尺寸与紧贴关系 --- */
SDRAM_STATIC_ASSERT(SDRAM_LAYER0_FB_SIZE == 0x00177000UL, "Layer0 FB size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAYER0_FB_END == (uintptr_t)0xD0177000UL, "Layer0 FB end mismatch");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LAYER0_FB_END, SDRAM_LVGL_FB_A_BASE),
                    "Layer0 FB must be followed by LVGL FB_A");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_A_SIZE == 0x00177000UL, "LVGL FB_A size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_A_END == (uintptr_t)0xD02EE000UL, "LVGL FB_A end mismatch");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LVGL_FB_A_END, SDRAM_LVGL_FB_B_BASE),
                    "LVGL FB_A must be followed by LVGL FB_B");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_B_SIZE == 0x00177000UL, "LVGL FB_B size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_B_END == (uintptr_t)0xD0465000UL, "LVGL FB_B end mismatch");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LVGL_FB_B_END, SDRAM_LAUNCHER_STRIP_BASE),
                    "LVGL FB_B must be followed by Launcher strip arena");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_STRIP_BASE == (uintptr_t)0xD0465000UL, "Launcher strip base mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_STRIP_ARENA_END == (uintptr_t)0xD0865000UL, "Launcher strip arena end mismatch");

/* --- Launcher strip 几何 --- */
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_ROW_BYTES == 10640UL, "Launcher strip row bytes mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_STRIDE_BYTES == 10656UL, "Launcher strip stride mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_STRIDE_PIXELS == 2664UL, "Launcher strip stride pixels mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_ALLOC_SIZE == 0x0038E8C0UL, "Launcher strip alloc size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(LAUNCHER_STRIP_STRIDE_BYTES, 32UL), "Launcher strip stride must be 32B aligned");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_STRIDE_BYTES >= LAUNCHER_STRIP_ROW_BYTES, "Launcher strip stride must cover logical row");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_ALLOC_SIZE <= SDRAM_LAUNCHER_STRIP_ARENA_SIZE, "Launcher strip must fit in its arena");
SDRAM_STATIC_ASSERT(LAUNCHER_STRIP_ARENA_MARGIN_BYTES == 0x00071740UL, "Launcher strip arena margin mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_SCROLL_MAX_X == 1860UL, "Launcher scroll max X mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_SCROLL_MAX_VISIBLE_X == 2659UL, "Launcher max visible X mismatch");
SDRAM_STATIC_ASSERT(LAUNCHER_SCROLL_MAX_VISIBLE_X < LAUNCHER_STRIP_WIDTH, "Launcher viewport exceeds logical strip width");
SDRAM_STATIC_ASSERT(LAUNCHER_VIEWPORT_HEIGHT <= LAUNCHER_STRIP_HEIGHT, "Launcher viewport taller than strip");

/* --- strip 之后的既有固定 region --- */
SDRAM_STATIC_ASSERT(SDRAM_LVGL_HEAP_SIZE == 0x01000000UL, "LVGL heap size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_DMA_POOL_SIZE == 0x00400000UL, "DMA pool size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_CACHE_SIZE == 0x00400000UL, "Launcher cache size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LVGL_HEAP_BASE),
                    "LVGL heap must follow the strip arena");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LVGL_HEAP_END, SDRAM_DMA_POOL_BASE),
                    "DMA pool must follow the LVGL heap");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_DMA_POOL_END, SDRAM_LAUNCHER_CACHE_BASE),
                    "Launcher cache must follow the DMA pool");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(SDRAM_LAUNCHER_CACHE_END, SDRAM_APP_ARENA_BASE),
                    "APP_ARENA_REST must follow the launcher cache");

/* --- APP_ARENA_REST 内部分区 --- */
SDRAM_STATIC_ASSERT(LUA_HEAP_SIZE == 0x00200000UL, "Lua heap size mismatch");
SDRAM_STATIC_ASSERT(COLD_POOL_SIZE == 0x00800000UL, "Cold pool size mismatch");
SDRAM_STATIC_ASSERT(SDRAM_REGION_TIGHTLY_FOLLOWS(LUA_HEAP_END + 1UL, RESOURCE_ARENA_BASE),
                    "Resource arena must follow the Lua heap");
SDRAM_STATIC_ASSERT(RESOURCE_ARENA_END + 1UL == COLD_POOL_BASE, "Cold pool must follow the resource arena");
SDRAM_STATIC_ASSERT(RESOURCE_ARENA_BASE < RESOURCE_ARENA_END, "Resource arena must be non-empty");
SDRAM_STATIC_ASSERT(SDRAM_APP_ARENA_SIZE == (LUA_HEAP_SIZE + RESOURCE_ARENA_SIZE + COLD_POOL_SIZE),
                    "APP_ARENA_REST split mismatch");
SDRAM_STATIC_ASSERT(SDRAM_ADDR_IN_RANGE_CHK(LUA_HEAP_BASE, SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END), "Lua heap outside APP_ARENA_REST");
SDRAM_STATIC_ASSERT(SDRAM_ADDR_IN_RANGE_CHK(COLD_POOL_END, SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END), "Cold pool outside APP_ARENA_REST");

/* --- 全部 region 的不重叠检查（含贯穿全空间的最终边界） --- */
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LAYER0_FB_END, SDRAM_LVGL_FB_A_BASE), "Layer0 FB overlaps LVGL FB_A");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LVGL_FB_A_END, SDRAM_LVGL_FB_B_BASE), "LVGL FB_A overlaps LVGL FB_B");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LVGL_FB_B_END, SDRAM_LAUNCHER_STRIP_BASE), "LVGL FB_B overlaps Launcher strip");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LVGL_HEAP_BASE), "Launcher strip overlaps LVGL heap");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LVGL_HEAP_END, SDRAM_DMA_POOL_BASE), "LVGL heap overlaps DMA pool");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_DMA_POOL_END, SDRAM_LAUNCHER_CACHE_BASE), "DMA pool overlaps launcher cache");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_LAUNCHER_CACHE_END, SDRAM_APP_ARENA_BASE), "Launcher cache overlaps APP_ARENA_REST");
SDRAM_STATIC_ASSERT(SDRAM_REGION_NO_OVERLAP(SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END + 1UL), "APP_ARENA_REST exceeds SDRAM end");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_STRIP_ARENA_END <= SDRAM_LIMIT_ADDR, "Launcher strip arena exceeds SDRAM");
SDRAM_STATIC_ASSERT(SDRAM_LAUNCHER_CACHE_END <= SDRAM_LIMIT_ADDR, "Launcher cache exceeds SDRAM");
SDRAM_STATIC_ASSERT(SDRAM_APP_ARENA_END + 1UL <= SDRAM_LIMIT_ADDR, "APP_ARENA_REST exceeds SDRAM end-exclusive");

/* --- 对齐：framebuffer / strip / DMA / arena --- */
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAYER0_FB_BASE, SDRAM_FB_ALIGN), "Layer0 FB base not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAYER0_FB_END, SDRAM_FB_ALIGN), "Layer0 FB end not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_FB_A_BASE, SDRAM_FB_ALIGN), "LVGL FB_A base not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_FB_A_END, SDRAM_FB_ALIGN), "LVGL FB_A end not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_FB_B_BASE, SDRAM_FB_ALIGN), "LVGL FB_B base not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_FB_B_END, SDRAM_FB_ALIGN), "LVGL FB_B end not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAUNCHER_STRIP_BASE, SDRAM_FB_ALIGN), "Launcher strip base not 256B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_DEFAULT_ALIGN), "Launcher strip arena end not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_HEAP_BASE, SDRAM_DEFAULT_ALIGN), "LVGL heap base not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LVGL_HEAP_END, SDRAM_DEFAULT_ALIGN), "LVGL heap end not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_DMA_POOL_BASE, SDRAM_DMA_ALIGN), "DMA pool base not 64B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_DMA_POOL_END, SDRAM_DMA_ALIGN), "DMA pool end not 64B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAUNCHER_CACHE_BASE, SDRAM_DEFAULT_ALIGN), "Launcher cache base not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_LAUNCHER_CACHE_END, SDRAM_DEFAULT_ALIGN), "Launcher cache end not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(SDRAM_APP_ARENA_BASE, SDRAM_DEFAULT_ALIGN), "APP_ARENA_REST base not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(LUA_HEAP_BASE, SDRAM_DEFAULT_ALIGN), "Lua heap base not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(RESOURCE_ARENA_BASE, SDRAM_DEFAULT_ALIGN), "Resource arena base not 32B aligned");
SDRAM_STATIC_ASSERT(SDRAM_U32_IS_ALIGNED(COLD_POOL_BASE, SDRAM_DEFAULT_ALIGN), "Cold pool base not 32B aligned");

/* 重排基线：前三块 framebuffer 必须保持历史地址不变 */
SDRAM_STATIC_ASSERT(SDRAM_LAYER0_FB_BASE == SDRAM_LAYOUT_FROZEN_LAYER0_FB_BASE, "Layer0 FB must stay at historical base");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_A_BASE == SDRAM_LAYOUT_FROZEN_LVGL_FB_A_BASE, "LVGL FB_A must stay at historical base");
SDRAM_STATIC_ASSERT(SDRAM_LVGL_FB_B_BASE == SDRAM_LAYOUT_FROZEN_LVGL_FB_B_BASE, "LVGL FB_B must stay at historical base");

#undef SDRAM_STATIC_ASSERT

#endif /* SDRAM_LAYOUT_H */
