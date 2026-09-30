#ifndef LAUNCHER_CACHE_RENDER_H
#define LAUNCHER_CACHE_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Launcher 缓存渲染器。
 *
 * 用 LVGL 公开的 offscreen snapshot API 把现有 object tree 一次性烘焙成两张
 * 固定缓存，之后 Launcher 的每帧显示完全交给 LTDC：
 *
 *   Layer0 固定 UI  -> SDRAM_LAYER0_FB_BASE (800x480, stride 3200)
 *   Launcher strip  -> SDRAM_LAUNCHER_STRIP_BASE (2660x350, stride 10656)
 *
 * 不做任何动态分配：draw buffer 直接包住 SDRAM 固定区间
 * (lv_draw_buf_init + header.flags=0，因此 LVGL 不会释放它们)。
 *
 * stride 能对上是因为本项目 LV_DRAW_BUF_STRIDE_ALIGN=32，而
 *   lv_draw_buf_width_to_stride(800, ARGB8888)  = 3200
 *   lv_draw_buf_width_to_stride(2660, ARGB8888) = 10656
 * 与 sdram_layout.h 预留的 stride 完全一致。
 *
 * 未修改 LVGL renderer/refresh 核心，只启用了 LV_USE_SNAPSHOT 模块开关。
 */

typedef enum {
    LAUNCHER_CACHE_OK = 0,
    LAUNCHER_CACHE_ERR_NOT_INITIALIZED,
    LAUNCHER_CACHE_ERR_NULL_OBJECT,
    LAUNCHER_CACHE_ERR_GEOMETRY,      /* object 尺寸与目标缓存不匹配 */
    LAUNCHER_CACHE_ERR_STRIDE,        /* LVGL 自然 stride 与 SDRAM 预留不一致 */
    LAUNCHER_CACHE_ERR_SNAPSHOT,      /* lv_snapshot_take_to_draw_buf 失败 */
} launcher_cache_result_t;

typedef struct {
    uint32_t static_builds;
    uint32_t static_failures;
    uint32_t strip_builds;
    uint32_t strip_failures;
    uint32_t last_static_build_ms;
    uint32_t last_strip_build_ms;
    uint32_t max_static_build_ms;
    uint32_t max_strip_build_ms;
    launcher_cache_result_t last_static_result;
    launcher_cache_result_t last_strip_result;
    uint32_t stride_check_failures;
    bool initialized;
} launcher_cache_stats_t;

/** 复位统计并校验 LVGL 自然 stride 与 SDRAM 预留一致 */
void launcher_cache_render_init(void);

/**
 * @brief  把固定 UI（不含滚动内容）渲染到指定缓存
 * @param  fixed_root   固定 UI 根对象（如 s_main_container）
 * @param  hidden_root  渲染期间需要临时隐藏的滚动内容根对象；可为 NULL
 * @param  dst_addr     目标缓存地址（LAYER0_FB 或 FB_A staging）
 * @param  width/height 目标缓存尺寸（必须等于 fixed_root 的尺寸）
 * @return 结果码；非 OK 时调用方必须走 fallback
 * @note   渲染期间临时关闭 display invalidation，避免污染当前 scanout 与
 *         产生无意义的重绘；结束后恢复原状态
 */
launcher_cache_result_t launcher_cache_render_static(lv_obj_t *fixed_root,
                                                     lv_obj_t *hidden_root,
                                                     uintptr_t dst_addr,
                                                     uint32_t width,
                                                     uint32_t height);

/**
 * @brief  把滚动内容渲染到 Launcher strip 缓存
 * @param  content      content_container（尺寸必须等于 2660x350）
 * @param  dst_addr     目标缓存地址（SDRAM_LAUNCHER_STRIP_BASE）
 * @param  width/height 目标缓存尺寸
 */
launcher_cache_result_t launcher_cache_render_strip(lv_obj_t *content,
                                                    uintptr_t dst_addr,
                                                    uint32_t width,
                                                    uint32_t height);

const launcher_cache_stats_t *launcher_cache_render_stats(void);

/** 结果码名字（日志/dump 用） */
const char *launcher_cache_result_name(launcher_cache_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_CACHE_RENDER_H */
