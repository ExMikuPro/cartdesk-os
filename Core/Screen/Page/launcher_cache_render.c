#include "launcher_cache_render.h"

#include <string.h>

#include "main.h"
#include "sdram_layout.h"

static launcher_cache_stats_t s_stats;

/* 包住 SDRAM 固定区间的 draw buffer；header.flags 保持 0，LVGL 不会释放它们。 */
static lv_draw_buf_t s_static_draw_buf;
static lv_draw_buf_t s_strip_draw_buf;
static bool s_static_draw_buf_ready;
static bool s_strip_draw_buf_ready;

const char *launcher_cache_result_name(launcher_cache_result_t result)
{
    switch (result) {
    case LAUNCHER_CACHE_OK:                 return "OK";
    case LAUNCHER_CACHE_ERR_NOT_INITIALIZED:return "NOT_INITIALIZED";
    case LAUNCHER_CACHE_ERR_NULL_OBJECT:    return "NULL_OBJECT";
    case LAUNCHER_CACHE_ERR_GEOMETRY:       return "GEOMETRY";
    case LAUNCHER_CACHE_ERR_STRIDE:         return "STRIDE";
    case LAUNCHER_CACHE_ERR_SNAPSHOT:       return "SNAPSHOT";
    default:                                return "?";
    }
}

static bool s_initialized;

void launcher_cache_render_init(void)
{
    const bool first = !s_initialized;
    s_initialized = true;

    if (first) {
        (void)memset(&s_stats, 0, sizeof(s_stats));
    }
    s_stats.last_static_result = LAUNCHER_CACHE_OK;
    s_stats.last_strip_result = LAUNCHER_CACHE_OK;
    s_static_draw_buf_ready = false;
    s_strip_draw_buf_ready = false;

    /* LVGL 自然 stride 必须与 sdram_layout.h 预留完全一致，否则缓存布局会错位。 */
    const uint32_t static_stride = lv_draw_buf_width_to_stride(
        (uint32_t)SDRAM_LAYER0_FB_WIDTH, LV_COLOR_FORMAT_ARGB8888);
    const uint32_t strip_stride = lv_draw_buf_width_to_stride(
        (uint32_t)LAUNCHER_STRIP_WIDTH, LV_COLOR_FORMAT_ARGB8888);

    if (static_stride != ((uint32_t)SDRAM_LAYER0_FB_WIDTH * 4u) ||
        strip_stride != (uint32_t)LAUNCHER_STRIP_STRIDE_BYTES) {
        ++s_stats.stride_check_failures;
        s_stats.initialized = false;
        return;
    }

    s_stats.initialized = true;
}

const launcher_cache_stats_t *launcher_cache_render_stats(void)
{
    return &s_stats;
}

/**
 * 渲染前检查：object 尺寸必须等于目标缓存尺寸，且没有外延绘制。
 * 尺寸不符或需要外延空间时不能截断渲染，必须让调用方走 fallback。
 */
static launcher_cache_result_t check_geometry(lv_obj_t *obj,
                                              uint32_t width,
                                              uint32_t height)
{
    if (obj == NULL) {
        return LAUNCHER_CACHE_ERR_NULL_OBJECT;
    }

    lv_obj_update_layout(obj);

    if ((uint32_t)lv_obj_get_width(obj) != width ||
        (uint32_t)lv_obj_get_height(obj) != height) {
        return LAUNCHER_CACHE_ERR_GEOMETRY;
    }

    return LAUNCHER_CACHE_OK;
}

static launcher_cache_result_t bind_draw_buf(lv_draw_buf_t *draw_buf,
                                             bool *ready,
                                             uintptr_t dst_addr,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t stride)
{
    if (*ready) {
        return LAUNCHER_CACHE_OK;
    }

    void *data = (void *)dst_addr;
    if (lv_draw_buf_init(draw_buf,
                         width,
                         height,
                         LV_COLOR_FORMAT_ARGB8888,
                         stride,
                         data,
                         stride * height) != LV_RESULT_OK) {
        return LAUNCHER_CACHE_ERR_SNAPSHOT;
    }

    *ready = true;
    return LAUNCHER_CACHE_OK;
}

launcher_cache_result_t launcher_cache_render_static(lv_obj_t *fixed_root,
                                                     lv_obj_t *hidden_root,
                                                     uintptr_t dst_addr,
                                                     uint32_t width,
                                                     uint32_t height)
{
    if (!s_stats.initialized) {
        s_stats.last_static_result = LAUNCHER_CACHE_ERR_NOT_INITIALIZED;
        return LAUNCHER_CACHE_ERR_NOT_INITIALIZED;
    }

    launcher_cache_result_t result = check_geometry(fixed_root, width, height);
    if (result != LAUNCHER_CACHE_OK) {
        ++s_stats.stride_check_failures;
        s_stats.last_static_result = result;
        return result;
    }

    const uint32_t stride = (uint32_t)SDRAM_LAYER0_FB_WIDTH * 4u;
    result = bind_draw_buf(&s_static_draw_buf, &s_static_draw_buf_ready,
                           dst_addr, width, height, stride);
    if (result != LAUNCHER_CACHE_OK) {
        s_stats.last_static_result = result;
        return result;
    }

    lv_display_t *disp = lv_obj_get_display(fixed_root);
    const bool invalidation_was_enabled =
        (disp != NULL) && lv_display_is_invalidation_enabled(disp);

    /* 隐藏滚动内容期间不允许产生任何真实失效：snapshot 走独立 layer。 */
    if (disp != NULL) {
        lv_display_enable_invalidation(disp, false);
    }

    const bool hidden_was_hidden = (hidden_root != NULL) && lv_obj_is_hidden(hidden_root);
    if (hidden_root != NULL) {
        lv_obj_set_hidden(hidden_root, true);
    }

    const uint32_t start_ms = HAL_GetTick();
    const lv_result_t snap = lv_snapshot_take_to_draw_buf(fixed_root,
                                                          LV_COLOR_FORMAT_ARGB8888,
                                                          &s_static_draw_buf);
    const uint32_t elapsed_ms = HAL_GetTick() - start_ms;

    if (hidden_root != NULL && !hidden_was_hidden) {
        lv_obj_set_hidden(hidden_root, false);
    }

    if (disp != NULL) {
        lv_display_enable_invalidation(disp, invalidation_was_enabled);
    }

    if (snap != LV_RESULT_OK) {
        ++s_stats.static_failures;
        if (elapsed_ms > s_stats.max_static_build_ms) {
            s_stats.max_static_build_ms = elapsed_ms;
        }
        s_stats.last_static_result = LAUNCHER_CACHE_ERR_SNAPSHOT;
        return LAUNCHER_CACHE_ERR_SNAPSHOT;
    }

    ++s_stats.static_builds;
    s_stats.last_static_build_ms = elapsed_ms;
    if (elapsed_ms > s_stats.max_static_build_ms) {
        s_stats.max_static_build_ms = elapsed_ms;
    }
    s_stats.last_static_result = LAUNCHER_CACHE_OK;
    return LAUNCHER_CACHE_OK;
}

launcher_cache_result_t launcher_cache_render_strip(lv_obj_t *content,
                                                    uintptr_t dst_addr,
                                                    uint32_t width,
                                                    uint32_t height)
{
    if (!s_stats.initialized) {
        s_stats.last_strip_result = LAUNCHER_CACHE_ERR_NOT_INITIALIZED;
        return LAUNCHER_CACHE_ERR_NOT_INITIALIZED;
    }

    launcher_cache_result_t result = check_geometry(content, width, height);
    if (result != LAUNCHER_CACHE_OK) {
        ++s_stats.strip_failures;
        s_stats.last_strip_result = result;
        return result;
    }

    const uint32_t stride = (uint32_t)LAUNCHER_STRIP_STRIDE_BYTES;
    result = bind_draw_buf(&s_strip_draw_buf, &s_strip_draw_buf_ready,
                           dst_addr, width, height, stride);
    if (result != LAUNCHER_CACHE_OK) {
        ++s_stats.strip_failures;
        s_stats.last_strip_result = result;
        return result;
    }

    const uint32_t start_ms = HAL_GetTick();
    const lv_result_t snap = lv_snapshot_take_to_draw_buf(content,
                                                          LV_COLOR_FORMAT_ARGB8888,
                                                          &s_strip_draw_buf);
    const uint32_t elapsed_ms = HAL_GetTick() - start_ms;

    if (snap != LV_RESULT_OK) {
        ++s_stats.strip_failures;
        s_stats.last_strip_result = LAUNCHER_CACHE_ERR_SNAPSHOT;
        return LAUNCHER_CACHE_ERR_SNAPSHOT;
    }

    ++s_stats.strip_builds;
    s_stats.last_strip_build_ms = elapsed_ms;
    if (elapsed_ms > s_stats.max_strip_build_ms) {
        s_stats.max_strip_build_ms = elapsed_ms;
    }
    s_stats.last_strip_result = LAUNCHER_CACHE_OK;
    return LAUNCHER_CACHE_OK;
}
