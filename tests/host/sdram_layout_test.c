/*
 * Host-side regression test for the unified SDRAM layout.
 *
 * 本测试直接包含 production 的 Core/Inc/sdram_layout.h（不经过任何 host stub），
 * 因此布局的编译期断言本身也是测试的一部分：任何相邻 region 重叠、紧贴关系
 * 被破坏、对齐不满足、Launcher strip 几何算错，都会在编译阶段失败。
 *
 * 运行时部分额外验证：
 *   - 64 MiB 总容量与 end-exclusive 边界
 *   - 前四块 region 的顺序与地址（不可调整）
 *   - 全部固定 region 的 start/size/end/alignment 与无 overlap
 *   - RESOURCE_ARENA 吃到 SDRAM 末端
 *   - Launcher strip 几何、stride padding 与最大 panning 边界
 *   - 与 xhgc_memory_layout 分区表元数据的一致性
 * 并打印完整 layout table。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sdram_layout.h"
#include "xhgc_memory_layout.h"

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                       \
    do {                                                                     \
        const uint32_t check_actual = (uint32_t)(actual);                    \
        const uint32_t check_expected = (uint32_t)(expected);                \
        ++g_checks;                                                          \
        if (check_actual != check_expected) {                                \
            ++g_failures;                                                    \
            printf("FAIL %s:%d: %s = 0x%08lX, expected 0x%08lX\n",           \
                   __FILE__, __LINE__, #actual,                              \
                   (unsigned long)check_actual,                              \
                   (unsigned long)check_expected);                           \
        }                                                                    \
    } while (0)

typedef struct {
    const char *name;
    uintptr_t base;
    uintptr_t end;
    uint32_t size;
    uint32_t align;
    int fixed;
} LayoutRegion;

static int region_is_aligned(uintptr_t value, uint32_t align)
{
    return align != 0u && (value & ((uintptr_t)align - 1u)) == 0u;
}

static void check_region(const LayoutRegion *region)
{
    CHECK(region->base < region->end);
    CHECK_EQ_U32(region->end - region->base, region->size);
    CHECK(region_is_aligned(region->base, region->align));
    CHECK(region_is_aligned(region->end, region->align));
    CHECK(region->base >= SDRAM_BASE_ADDR);
    CHECK(region->end <= SDRAM_LIMIT_ADDR);
}

static void test_total_size(void)
{
    CHECK_EQ_U32(SDRAM_TOTAL_SIZE, 0x04000000UL);
    CHECK_EQ_U32(SDRAM_END_ADDR - SDRAM_BASE_ADDR + 1UL, SDRAM_TOTAL_SIZE);
    CHECK(SDRAM_LIMIT_ADDR == (uintptr_t)0xD4000000UL);
    CHECK(SDRAM_END_ADDR == (uintptr_t)0xD3FFFFFFUL);
}

static void test_front_four_regions(void)
{
    /* 前四块顺序与地址是冻结契约，不允许改动 */
    CHECK(SDRAM_LAYER0_FB_BASE == (uintptr_t)0xD0000000UL);
    CHECK(SDRAM_LAYER0_FB_END == (uintptr_t)0xD0177000UL);
    CHECK_EQ_U32(SDRAM_LAYER0_FB_SIZE, 0x00177000UL);

    CHECK(SDRAM_LVGL_FB_A_BASE == (uintptr_t)0xD0177000UL);
    CHECK(SDRAM_LVGL_FB_A_END == (uintptr_t)0xD02EE000UL);
    CHECK_EQ_U32(SDRAM_LVGL_FB_A_SIZE, 0x00177000UL);

    CHECK(SDRAM_LVGL_FB_B_BASE == (uintptr_t)0xD02EE000UL);
    CHECK(SDRAM_LVGL_FB_B_END == (uintptr_t)0xD0465000UL);
    CHECK_EQ_U32(SDRAM_LVGL_FB_B_SIZE, 0x00177000UL);

    CHECK(SDRAM_LAUNCHER_STRIP_BASE == (uintptr_t)0xD0465000UL);
    CHECK(SDRAM_LAUNCHER_STRIP_ARENA_END == (uintptr_t)0xD0865000UL);
    CHECK_EQ_U32(SDRAM_LAUNCHER_STRIP_ARENA_SIZE, 0x00400000UL);

    /* 严格首尾相接 */
    CHECK(SDRAM_LAYER0_FB_END == SDRAM_LVGL_FB_A_BASE);
    CHECK(SDRAM_LVGL_FB_A_END == SDRAM_LVGL_FB_B_BASE);
    CHECK(SDRAM_LVGL_FB_B_END == SDRAM_LAUNCHER_STRIP_BASE);
}

static void test_launcher_strip_geometry(void)
{
    CHECK_EQ_U32(LAUNCHER_STRIP_WIDTH, 2660UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_HEIGHT, 350UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_BPP, 4UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_ROW_BYTES, 10640UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_STRIDE_BYTES, 10656UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_STRIDE_PIXELS, 2664UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_STRIDE_BYTES % 32UL, 0UL);
    CHECK(LAUNCHER_STRIP_STRIDE_BYTES >= LAUNCHER_STRIP_ROW_BYTES);
    CHECK_EQ_U32(LAUNCHER_STRIP_STRIDE_BYTES - LAUNCHER_STRIP_ROW_BYTES, 16UL);

    CHECK_EQ_U32(LAUNCHER_STRIP_ALLOC_SIZE, 0x0038E8C0UL);
    CHECK(LAUNCHER_STRIP_ALLOC_SIZE <= SDRAM_LAUNCHER_STRIP_ARENA_SIZE);
    CHECK_EQ_U32(LAUNCHER_STRIP_ARENA_MARGIN_BYTES, 0x00071740UL);
    CHECK_EQ_U32(LAUNCHER_STRIP_ALLOC_SIZE + LAUNCHER_STRIP_ARENA_MARGIN_BYTES,
                 SDRAM_LAUNCHER_STRIP_ARENA_SIZE);

    CHECK_EQ_U32(LAUNCHER_VIEWPORT_WIDTH, 800UL);
    CHECK_EQ_U32(LAUNCHER_VIEWPORT_HEIGHT, 350UL);
    CHECK_EQ_U32(LAUNCHER_SCROLL_MAX_X, 1860UL);
    CHECK_EQ_U32(LAUNCHER_SCROLL_MAX_VISIBLE_X, 2659UL);
    CHECK(LAUNCHER_SCROLL_MAX_VISIBLE_X < LAUNCHER_STRIP_WIDTH);

    /* 最大 scroll 的 viewport 行尾仍落在 stride 内（padding 不被当作可见内容） */
    CHECK((LAUNCHER_SCROLL_MAX_VISIBLE_X + 1UL) * LAUNCHER_STRIP_BPP <=
          LAUNCHER_STRIP_STRIDE_BYTES);
    /* stride 尾部 padding 像素数 = 4 */
    CHECK_EQ_U32(LAUNCHER_STRIP_STRIDE_PIXELS - LAUNCHER_STRIP_WIDTH, 4UL);
}

static void test_region_table(void)
{
    static const LayoutRegion regions[] = {
        {"LAYER0_FB", SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END, SDRAM_LAYER0_FB_SIZE, SDRAM_FB_ALIGN, 1},
        {"LVGL_FB_A", SDRAM_LVGL_FB_A_BASE, SDRAM_LVGL_FB_A_END, SDRAM_LVGL_FB_A_SIZE, SDRAM_FB_ALIGN, 1},
        {"LVGL_FB_B", SDRAM_LVGL_FB_B_BASE, SDRAM_LVGL_FB_B_END, SDRAM_LVGL_FB_B_SIZE, SDRAM_FB_ALIGN, 1},
        {"LAUNCHER_STRIP", SDRAM_LAUNCHER_STRIP_BASE, SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LAUNCHER_STRIP_ARENA_SIZE, SDRAM_FB_ALIGN, 1},
        {"SDRAM_LVGL_HEAP", SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END, SDRAM_LVGL_HEAP_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"DMA_POOL", SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END, SDRAM_DMA_POOL_SIZE, SDRAM_DMA_ALIGN, 1},
        {"LAUNCHER_CACHE", SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END, SDRAM_LAUNCHER_CACHE_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"APP_ARENA_REST", SDRAM_APP_ARENA_BASE, (uintptr_t)(SDRAM_APP_ARENA_END + 1UL), SDRAM_APP_ARENA_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"LUA_HEAP", LUA_HEAP_BASE, (uintptr_t)(LUA_HEAP_END + 1UL), LUA_HEAP_SIZE, SDRAM_DEFAULT_ALIGN, 0},
        {"RESOURCE_ARENA", RESOURCE_ARENA_BASE, (uintptr_t)(RESOURCE_ARENA_END + 1UL), RESOURCE_ARENA_SIZE, SDRAM_DEFAULT_ALIGN, 0},
        {"COLD_POOL", COLD_POOL_BASE, (uintptr_t)(COLD_POOL_END + 1UL), COLD_POOL_SIZE, SDRAM_DEFAULT_ALIGN, 0},
    };
    const size_t count = sizeof(regions) / sizeof(regions[0]);

    for (size_t i = 0u; i < count; ++i) {
        check_region(&regions[i]);
    }

    /* 顶层固定 region 两两不重叠（子 region 合法地嵌在 APP_ARENA_REST 内） */
    for (size_t i = 0u; i < 8u; ++i) {
        for (size_t j = i + 1u; j < 8u; ++j) {
            const LayoutRegion *a = &regions[i];
            const LayoutRegion *b = &regions[j];
            const int a_before_b = a->end <= b->base;
            const int b_before_a = b->end <= a->base;
            CHECK(a_before_b || b_before_a);
        }
    }

    /* 固定前 8 块必须首尾相接，整体覆盖 0xD0000000~0xD4000000 */
    CHECK(regions[0].base == SDRAM_BASE_ADDR);
    CHECK(regions[7].end == SDRAM_LIMIT_ADDR);
    for (size_t i = 0u; i + 1u < 8u; ++i) {
        CHECK(regions[i].end == regions[i + 1u].base);
    }

    /* 子 region 必须落在 APP_ARENA_REST 内 */
    CHECK(regions[8].base == regions[7].base);
    CHECK(regions[8].end == regions[9].base);
    CHECK(regions[9].end == regions[10].base);
    CHECK(regions[10].end == regions[7].end);
    CHECK_EQ_U32(LUA_HEAP_SIZE, 0x00200000UL);
    CHECK_EQ_U32(COLD_POOL_SIZE, 0x00800000UL);
}

static void test_resource_arena(void)
{
    CHECK(RESOURCE_ARENA_BASE == (uintptr_t)0xD2265000UL);
    CHECK(RESOURCE_ARENA_END == (uintptr_t)(SDRAM_LIMIT_ADDR - 1UL - (uintptr_t)COLD_POOL_SIZE));
    CHECK(RESOURCE_ARENA_END + 1UL == COLD_POOL_BASE);
    CHECK(RESOURCE_ARENA_BASE == LUA_HEAP_END + 1UL);
    CHECK_EQ_U32(RESOURCE_ARENA_SIZE, 0x0159B000UL);
    CHECK_EQ_U32(RESOURCE_ARENA_SIZE,
                 SDRAM_APP_ARENA_SIZE - LUA_HEAP_SIZE - COLD_POOL_SIZE);
    CHECK(RESOURCE_ARENA_BASE >= SDRAM_APP_ARENA_BASE);
    CHECK(RESOURCE_ARENA_END <= SDRAM_APP_ARENA_END);

    /* arena 必须落在 SDRAM 内且满足 32 byte 对齐 */
    CHECK(RESOURCE_ARENA_END + 1UL <= SDRAM_LIMIT_ADDR);
    CHECK(region_is_aligned(RESOURCE_ARENA_BASE, SDRAM_DEFAULT_ALIGN));
    CHECK(region_is_aligned(RESOURCE_ARENA_SIZE, SDRAM_DEFAULT_ALIGN));
}

static void test_address_helpers(void)
{
    CHECK(sdram_addr_in_layer0_fb(SDRAM_LAYER0_FB_BASE) != 0);
    CHECK(sdram_addr_in_layer0_fb(SDRAM_LAYER0_FB_END - 1UL) != 0);
    CHECK(sdram_addr_in_layer0_fb(SDRAM_LAYER0_FB_END) == 0);
    CHECK(sdram_addr_in_fb(SDRAM_LAYER0_FB_BASE) != 0);
    /* FB_A 的 base 就是 Layer0 FB 的 end-exclusive，两段恰好首尾相接 */
    CHECK(sdram_addr_in_fb(SDRAM_LAYER0_FB_END) != 0);
    CHECK(sdram_addr_in_layer0_fb(SDRAM_LAYER0_FB_END) == 0);
    CHECK(sdram_addr_in_fb(SDRAM_LVGL_FB_A_BASE) != 0);
    CHECK(sdram_addr_in_fb(SDRAM_LVGL_FB_B_BASE) != 0);
    CHECK(sdram_addr_in_fb(SDRAM_LVGL_FB_B_END) == 0);
    CHECK(sdram_addr_in_fb(SDRAM_LAUNCHER_STRIP_BASE) == 0);

    CHECK(sdram_addr_in_launcher_strip(SDRAM_LAUNCHER_STRIP_BASE) != 0);
    CHECK(sdram_addr_in_launcher_strip(SDRAM_LAUNCHER_STRIP_ARENA_END - 1UL) != 0);
    CHECK(sdram_addr_in_launcher_strip(SDRAM_LAUNCHER_STRIP_ARENA_END) == 0);

    CHECK(sdram_addr_in_lua_heap(LUA_HEAP_BASE) != 0);
    CHECK(sdram_addr_in_resource_arena(RESOURCE_ARENA_BASE) != 0);
    CHECK(sdram_addr_in_cold_pool(COLD_POOL_END) != 0);
}

static void test_zone_metadata_consistency(void)
{
    CHECK_EQ_U32(XHGC_MEM_ZONE_COUNT, 8u);
    CHECK(XHGC_SDRAM_BASE == SDRAM_BASE_ADDR);
    CHECK_EQ_U32(XHGC_SDRAM_SIZE, SDRAM_TOTAL_SIZE);
    CHECK(XHGC_SDRAM_END_EXCLUSIVE == SDRAM_LIMIT_ADDR);
    CHECK(xhgc_mem_layout_validate());

    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LAYER0_FB)->base == SDRAM_LAYER0_FB_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_A)->base == SDRAM_LVGL_FB_A_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_B)->base == SDRAM_LVGL_FB_B_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LAUNCHER_STRIP)->base == SDRAM_LAUNCHER_STRIP_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LAUNCHER_STRIP)->end == SDRAM_LAUNCHER_STRIP_ARENA_END);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_SDRAM_LVGL_HEAP)->base == SDRAM_LVGL_HEAP_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_DMA_POOL)->base == SDRAM_DMA_POOL_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LAUNCHER_CACHE)->base == SDRAM_LAUNCHER_CACHE_BASE);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_APP_ARENA_REST)->end == SDRAM_LIMIT_ADDR);

    CHECK(xhgc_mem_find_zone_by_addr(SDRAM_LAYER0_FB_BASE)->id == XHGC_MEM_ZONE_LAYER0_FB);
    CHECK(xhgc_mem_find_zone_by_addr(SDRAM_LVGL_FB_A_BASE)->id == XHGC_MEM_ZONE_LVGL_FB_A);
    CHECK(xhgc_mem_find_zone_by_addr(SDRAM_LVGL_FB_B_BASE)->id == XHGC_MEM_ZONE_LVGL_FB_B);
    CHECK(xhgc_mem_find_zone_by_addr(SDRAM_LAUNCHER_STRIP_BASE)->id == XHGC_MEM_ZONE_LAUNCHER_STRIP);
    CHECK(xhgc_mem_find_zone_by_addr(SDRAM_LIMIT_ADDR - 1UL)->id == XHGC_MEM_ZONE_APP_ARENA_REST);

    CHECK(xhgc_mem_addr_in_zone(XHGC_MEM_ZONE_LAUNCHER_STRIP, SDRAM_LAUNCHER_STRIP_BASE,
                                LAUNCHER_STRIP_ALLOC_SIZE));
    CHECK(xhgc_mem_addr_in_zone(XHGC_MEM_ZONE_DMA_POOL, SDRAM_DMA_POOL_BASE,
                                SDRAM_DMA_POOL_SIZE));
    CHECK(!xhgc_mem_addr_in_zone(XHGC_MEM_ZONE_LAUNCHER_STRIP, SDRAM_LAUNCHER_STRIP_BASE,
                                 SDRAM_LAUNCHER_STRIP_ARENA_SIZE + 1UL));

    CHECK(xhgc_mem_is_fixed_dma_target((const void *)SDRAM_LAYER0_FB_BASE, 16u));
    CHECK(xhgc_mem_is_fixed_dma_target((const void *)SDRAM_LAUNCHER_STRIP_BASE, 16u));
    CHECK(xhgc_mem_is_fixed_dma_target((const void *)SDRAM_LAUNCHER_CACHE_BASE, 16u));
    CHECK(xhgc_mem_is_fixed_dma_target((const void *)LUA_HEAP_BASE, 16u));
    CHECK(!xhgc_mem_is_fixed_dma_target((const void *)SDRAM_LAUNCHER_STRIP_ARENA_END, 16u));

    /* 前三块 framebuffer 的 base 不得互相覆盖 */
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LAYER0_FB)->end ==
          xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_A)->base);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_A)->end ==
          xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_B)->base);
    CHECK(xhgc_mem_get_zone(XHGC_MEM_ZONE_LVGL_FB_B)->end ==
          xhgc_mem_get_zone(XHGC_MEM_ZONE_LAUNCHER_STRIP)->base);
}

static void test_runtime_validator(void)
{
    /* 设备侧 sdram_layout_check() 调用的是同一个函数；这里跑它以保证
     * “host PASS 但设备 Error_Handler” 这类语义不一致不会再出现。 */
    const int failure = sdram_layout_runtime_validate();
    ++g_checks;
    if (failure != 0) {
        ++g_failures;
        printf("FAIL %s:%d: sdram_layout_runtime_validate() returned %d\n",
               __FILE__, __LINE__, failure);
    }

    /* 紧贴/对齐 helper 必须是 end-exclusive 语义 */
    CHECK(sdram_layout_tightly_follows(SDRAM_LAYER0_FB_END, SDRAM_LVGL_FB_A_BASE));
    CHECK(sdram_layout_tightly_follows(SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LVGL_HEAP_BASE));
    CHECK(!sdram_layout_tightly_follows(SDRAM_LAYER0_FB_END - 1UL, SDRAM_LVGL_FB_A_BASE));
    CHECK(sdram_layout_range_aligned(SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END, SDRAM_FB_ALIGN));
    CHECK(sdram_layout_range_aligned(SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END + 1UL, SDRAM_DEFAULT_ALIGN));
    CHECK(sdram_layout_range_aligned(LUA_HEAP_BASE, LUA_HEAP_END + 1UL, SDRAM_DEFAULT_ALIGN));
    CHECK(sdram_layout_range_aligned(RESOURCE_ARENA_BASE, RESOURCE_ARENA_END + 1UL, SDRAM_DEFAULT_ALIGN));
    CHECK(sdram_layout_range_aligned(COLD_POOL_BASE, COLD_POOL_END + 1UL, SDRAM_DEFAULT_ALIGN));
    /* LUA_HEAP_END 是 inclusive 地址，不能被当成 end-exclusive 使用 */
    CHECK(!sdram_layout_range_aligned(LUA_HEAP_BASE, LUA_HEAP_END, SDRAM_DEFAULT_ALIGN));
}

static void print_layout_table(void)
{
    static const LayoutRegion regions[] = {
        {"Layer0 Static FB", SDRAM_LAYER0_FB_BASE, SDRAM_LAYER0_FB_END, SDRAM_LAYER0_FB_SIZE, SDRAM_FB_ALIGN, 1},
        {"LVGL FB_A", SDRAM_LVGL_FB_A_BASE, SDRAM_LVGL_FB_A_END, SDRAM_LVGL_FB_A_SIZE, SDRAM_FB_ALIGN, 1},
        {"LVGL FB_B", SDRAM_LVGL_FB_B_BASE, SDRAM_LVGL_FB_B_END, SDRAM_LVGL_FB_B_SIZE, SDRAM_FB_ALIGN, 1},
        {"Launcher Strip Arena", SDRAM_LAUNCHER_STRIP_BASE, SDRAM_LAUNCHER_STRIP_ARENA_END, SDRAM_LAUNCHER_STRIP_ARENA_SIZE, SDRAM_FB_ALIGN, 1},
        {"  (strip used)", SDRAM_LAUNCHER_STRIP_BASE,
         SDRAM_LAUNCHER_STRIP_BASE + (uintptr_t)LAUNCHER_STRIP_ALLOC_SIZE,
         LAUNCHER_STRIP_ALLOC_SIZE, SDRAM_DEFAULT_ALIGN, 0},
        {"  (strip margin)", SDRAM_LAUNCHER_STRIP_BASE + (uintptr_t)LAUNCHER_STRIP_ALLOC_SIZE,
         SDRAM_LAUNCHER_STRIP_ARENA_END, LAUNCHER_STRIP_ARENA_MARGIN_BYTES, SDRAM_DEFAULT_ALIGN, 0},
        {"SDRAM LVGL heap", SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END, SDRAM_LVGL_HEAP_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"DMA pool", SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END, SDRAM_DMA_POOL_SIZE, SDRAM_DMA_ALIGN, 1},
        {"Launcher icon cache", SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END, SDRAM_LAUNCHER_CACHE_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"APP_ARENA_REST", SDRAM_APP_ARENA_BASE, (uintptr_t)(SDRAM_APP_ARENA_END + 1UL), SDRAM_APP_ARENA_SIZE, SDRAM_DEFAULT_ALIGN, 1},
        {"  Lua heap", LUA_HEAP_BASE, (uintptr_t)(LUA_HEAP_END + 1UL), LUA_HEAP_SIZE, SDRAM_DEFAULT_ALIGN, 0},
        {"  Resource Arena", RESOURCE_ARENA_BASE, (uintptr_t)(RESOURCE_ARENA_END + 1UL), RESOURCE_ARENA_SIZE, SDRAM_DEFAULT_ALIGN, 0},
        {"  Cold pool", COLD_POOL_BASE, (uintptr_t)(COLD_POOL_END + 1UL), COLD_POOL_SIZE, SDRAM_DEFAULT_ALIGN, 0},
    };
    const size_t count = sizeof(regions) / sizeof(regions[0]);

    printf("\n| Region | Start | End-exclusive | Size |\n");
    printf("|---|---:|---:|---:|\n");
    for (size_t i = 0u; i < count; ++i) {
        printf("| %s | 0x%08lX | 0x%08lX | 0x%08lX |\n",
               regions[i].name,
               (unsigned long)regions[i].base,
               (unsigned long)regions[i].end,
               (unsigned long)regions[i].size);
    }
    printf("\nLauncher strip: width=%lu height=%lu row=%lu stride=%lu (px=%lu) alloc=%lu margin=%lu scroll_max=%lu\n",
           (unsigned long)LAUNCHER_STRIP_WIDTH,
           (unsigned long)LAUNCHER_STRIP_HEIGHT,
           (unsigned long)LAUNCHER_STRIP_ROW_BYTES,
           (unsigned long)LAUNCHER_STRIP_STRIDE_BYTES,
           (unsigned long)LAUNCHER_STRIP_STRIDE_PIXELS,
           (unsigned long)LAUNCHER_STRIP_ALLOC_SIZE,
           (unsigned long)LAUNCHER_STRIP_ARENA_MARGIN_BYTES,
           (unsigned long)LAUNCHER_SCROLL_MAX_X);
}

int main(void)
{
    test_total_size();
    test_front_four_regions();
    test_launcher_strip_geometry();
    test_region_table();
    test_resource_arena();
    test_address_helpers();
    test_zone_metadata_consistency();
    test_runtime_validator();
    print_layout_table();

    printf("\n[SDRAM LAYOUT TEST] checks=%u failures=%u %s\n",
           g_checks, g_failures, g_failures == 0u ? "PASS" : "FAIL");

    return g_failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
