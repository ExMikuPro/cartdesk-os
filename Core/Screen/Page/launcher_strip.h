#ifndef LAUNCHER_STRIP_H
#define LAUNCHER_STRIP_H

#include <stdint.h>

#include "sdram_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Launcher horizontal cached strip 访问接口。
 *
 * 布局与几何全部来自 Core/Inc/sdram_layout.h，本文件只提供访问器和地址运算，
 * 不允许在 Launcher UI 代码里出现 0xD0465000 之类的绝对地址。
 *
 * 本模块只负责把正式预留的内存暴露出去，不实现 LTDC hardware panning，
 * 也不改变现有渲染路径；全部为 header-only inline，无额外编译单元。
 */

/** strip 缓存起始地址（对应 LTDC Layer 1 CFBAR 的 panning 原点） */
static inline uintptr_t launcher_strip_get_base(void)
{
    return SDRAM_LAUNCHER_STRIP_BASE;
}

/** 物理 stride（字节），已按 32 byte 对齐 = 10656 */
static inline uint32_t launcher_strip_get_stride(void)
{
    return LAUNCHER_STRIP_STRIDE_BYTES;
}

/** 物理 stride（像素），含尾部 padding = 2664 */
static inline uint32_t launcher_strip_get_stride_pixels(void)
{
    return LAUNCHER_STRIP_STRIDE_PIXELS;
}

/** strip arena 总容量（字节） = 4 MiB */
static inline uint32_t launcher_strip_get_capacity(void)
{
    return SDRAM_LAUNCHER_STRIP_ARENA_SIZE;
}

/** strip 实际几何需要占用的字节数 = stride * height */
static inline uint32_t launcher_strip_get_used_bytes(void)
{
    return LAUNCHER_STRIP_ALLOC_SIZE;
}

/** strip arena 中未使用的对齐 margin */
static inline uint32_t launcher_strip_get_margin_bytes(void)
{
    return LAUNCHER_STRIP_ARENA_MARGIN_BYTES;
}

/** 逻辑宽度（可见像素） = 2660 */
static inline uint32_t launcher_strip_get_width(void)
{
    return LAUNCHER_STRIP_WIDTH;
}

/** 逻辑高度 = 350 */
static inline uint32_t launcher_strip_get_height(void)
{
    return LAUNCHER_STRIP_HEIGHT;
}

/** 每像素字节数 = 4 */
static inline uint32_t launcher_strip_get_bpp(void)
{
    return LAUNCHER_STRIP_BPP;
}

/** 最大水平滚动量 = 1860（不是 2664 - 800） */
static inline uint32_t launcher_strip_get_scroll_max_x(void)
{
    return LAUNCHER_SCROLL_MAX_X;
}

/**
 * @brief  计算 strip 内任意像素的字节地址
 * @param  x: 逻辑像素 X，范围 [0, LAUNCHER_STRIP_WIDTH)
 * @param  y: 逻辑像素 Y，范围 [0, LAUNCHER_STRIP_HEIGHT)
 * @return 非0=该像素字节地址, 0=参数越界
 * @note   概念公式：BASE + y * STRIDE_BYTES + x * BPP
 */
static inline uintptr_t launcher_strip_pixel_address(uint32_t x, uint32_t y)
{
    if (x >= LAUNCHER_STRIP_WIDTH || y >= LAUNCHER_STRIP_HEIGHT) {
        return (uintptr_t)0;
    }

    return SDRAM_LAUNCHER_STRIP_BASE +
           ((uintptr_t)y * (uintptr_t)LAUNCHER_STRIP_STRIDE_BYTES) +
           ((uintptr_t)x * (uintptr_t)LAUNCHER_STRIP_BPP);
}

/**
 * @brief  计算 LTDC 水平平移后的 source 起始地址
 * @param  scroll_x: 水平滚动量，范围 [0, LAUNCHER_SCROLL_MAX_X]
 * @return 非0=source 起始地址, 0=参数越界
 * @note   对应 LTDC Layer 1 的 CFBAR；公式：BASE + scroll_x * BPP
 */
static inline uintptr_t launcher_strip_scroll_address(uint32_t scroll_x)
{
    if (scroll_x > LAUNCHER_SCROLL_MAX_X) {
        return (uintptr_t)0;
    }

    return SDRAM_LAUNCHER_STRIP_BASE +
           ((uintptr_t)scroll_x * (uintptr_t)LAUNCHER_STRIP_BPP);
}

/**
 * @brief  判断 strip 是否已位于预期布局区间内
 * @retval 1=base/stride/capacity 与统一布局一致
 * @note   仅做只读检查，供 Debug 启动自检使用
 */
static inline int launcher_strip_layout_is_valid(void)
{
    const uintptr_t base = launcher_strip_get_base();
    const uintptr_t arena_end = base + (uintptr_t)launcher_strip_get_capacity();

    if (base != SDRAM_LAUNCHER_STRIP_BASE ||
        arena_end != SDRAM_LAUNCHER_STRIP_ARENA_END) {
        return 0;
    }

    if ((launcher_strip_get_stride() % SDRAM_DEFAULT_ALIGN) != 0UL ||
        launcher_strip_get_stride() < LAUNCHER_STRIP_ROW_BYTES) {
        return 0;
    }

    if (launcher_strip_get_used_bytes() > launcher_strip_get_capacity()) {
        return 0;
    }

    if (launcher_strip_pixel_address(LAUNCHER_STRIP_WIDTH - 1UL,
                                     LAUNCHER_STRIP_HEIGHT - 1UL) >= arena_end) {
        return 0;
    }

    if (launcher_strip_scroll_address(LAUNCHER_SCROLL_MAX_X) +
        ((uintptr_t)LAUNCHER_VIEWPORT_WIDTH * (uintptr_t)LAUNCHER_STRIP_BPP) > arena_end) {
        return 0;
    }

    if (launcher_strip_scroll_address(LAUNCHER_SCROLL_MAX_X + 1UL) != 0UL) {
        return 0;
    }

    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_STRIP_H */
