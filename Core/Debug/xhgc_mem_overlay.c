#include "xhgc_mem_overlay.h"

#include <stdio.h>

#include "lvgl.h"
#include "xhgc_meminfo.h"

#define XHGC_MEM_OVERLAY_REFRESH_MS 1000u
#define XHGC_MEM_OVERLAY_TEXT_SIZE 512u

#ifndef XHGC_MEM_OVERLAY_BOOT_VISIBLE
#define XHGC_MEM_OVERLAY_BOOT_VISIBLE 0
#endif

static lv_obj_t *s_mem_overlay_label;
static bool s_mem_overlay_visible;
static uint32_t s_mem_overlay_last_update_ms;
static char s_mem_overlay_text[XHGC_MEM_OVERLAY_TEXT_SIZE];
static XHGC_MemInfoSnapshot s_mem_overlay_snapshot;

static bool xhgc_mem_overlay_lvgl_ready(void)
{
    return lv_is_initialized() && lv_display_get_default() != NULL;
}

static unsigned long xhgc_mem_overlay_mb_whole(uint32_t bytes)
{
    return (unsigned long)(bytes / (1024u * 1024u));
}

static unsigned long xhgc_mem_overlay_mb_frac(uint32_t bytes)
{
    uint32_t rem = bytes % (1024u * 1024u);
    return (unsigned long)((rem * 10u + (512u * 1024u)) / (1024u * 1024u));
}

static void xhgc_mem_overlay_format_pair(char *dst,
                                         size_t dst_size,
                                         const char *name,
                                         uint32_t used,
                                         uint32_t peak)
{
    unsigned long used_whole = xhgc_mem_overlay_mb_whole(used);
    unsigned long used_frac = xhgc_mem_overlay_mb_frac(used);
    unsigned long peak_whole = xhgc_mem_overlay_mb_whole(peak);
    unsigned long peak_frac = xhgc_mem_overlay_mb_frac(peak);

    if (used_frac >= 10u) {
        used_frac = 0u;
        ++used_whole;
    }
    if (peak_frac >= 10u) {
        peak_frac = 0u;
        ++peak_whole;
    }

    (void)snprintf(dst,
                   dst_size,
                   "%s %lu.%lu P%lu.%lu",
                   name,
                   used_whole,
                   used_frac,
                   peak_whole,
                   peak_frac);
}

static void xhgc_mem_overlay_create_label(void)
{
    lv_obj_t *parent;

    if (s_mem_overlay_label != NULL || !xhgc_mem_overlay_lvgl_ready()) {
        return;
    }

    parent = lv_layer_top();
    if (parent == NULL) {
        return;
    }

    s_mem_overlay_label = lv_label_create(parent);
    if (s_mem_overlay_label == NULL) {
        return;
    }

    s_mem_overlay_text[0] = '\0';
    lv_label_set_text_static(s_mem_overlay_label, "MEMINFO not ready");
    lv_label_set_long_mode(s_mem_overlay_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(s_mem_overlay_label, 176, 118);
    lv_obj_align(s_mem_overlay_label, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_set_style_bg_color(s_mem_overlay_label, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(s_mem_overlay_label, LV_OPA_70, 0);
    lv_obj_set_style_text_color(s_mem_overlay_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_left(s_mem_overlay_label, 5, 0);
    lv_obj_set_style_pad_right(s_mem_overlay_label, 5, 0);
    lv_obj_set_style_pad_top(s_mem_overlay_label, 4, 0);
    lv_obj_set_style_pad_bottom(s_mem_overlay_label, 4, 0);
    lv_obj_set_style_radius(s_mem_overlay_label, 3, 0);
    lv_obj_add_flag(s_mem_overlay_label, LV_OBJ_FLAG_HIDDEN);
}

void xhgc_mem_overlay_init(void)
{
    xhgc_mem_overlay_create_label();
    xhgc_mem_overlay_set_visible(XHGC_MEM_OVERLAY_BOOT_VISIBLE != 0);
}

void xhgc_mem_overlay_set_visible(bool visible)
{
    s_mem_overlay_visible = visible;

    xhgc_mem_overlay_create_label();
    if (s_mem_overlay_label == NULL) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(s_mem_overlay_label, LV_OBJ_FLAG_HIDDEN);
        s_mem_overlay_last_update_ms = 0u;
        xhgc_mem_overlay_update();
    } else {
        lv_obj_add_flag(s_mem_overlay_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void xhgc_mem_overlay_toggle(void)
{
    xhgc_mem_overlay_set_visible(!s_mem_overlay_visible);
}

bool xhgc_mem_overlay_is_visible(void)
{
    return s_mem_overlay_visible;
}

void xhgc_mem_overlay_update(void)
{
    uint32_t now;
    char app_line[32];
    char dma_line[32];
    char lua_line[32];
    char res_line[32];
    char lvgl_line[32];
    unsigned long total_used_whole;
    unsigned long total_used_frac;
    unsigned long total_whole;
    unsigned long total_frac;

    if (!s_mem_overlay_visible) {
        return;
    }

    xhgc_mem_overlay_create_label();
    if (s_mem_overlay_label == NULL || !xhgc_mem_overlay_lvgl_ready()) {
        return;
    }

    now = lv_tick_get();
    if (s_mem_overlay_last_update_ms != 0u &&
        (uint32_t)(now - s_mem_overlay_last_update_ms) < XHGC_MEM_OVERLAY_REFRESH_MS) {
        return;
    }
    s_mem_overlay_last_update_ms = now;

    xhgc_meminfo_get_snapshot(&s_mem_overlay_snapshot);
    if (s_mem_overlay_snapshot.total_sdram == 0u) {
        lv_label_set_text_static(s_mem_overlay_label, "MEMINFO not ready");
        return;
    }

    total_used_whole = xhgc_mem_overlay_mb_whole(s_mem_overlay_snapshot.total_used);
    total_used_frac = xhgc_mem_overlay_mb_frac(s_mem_overlay_snapshot.total_used);
    total_whole = xhgc_mem_overlay_mb_whole(s_mem_overlay_snapshot.total_sdram);
    total_frac = xhgc_mem_overlay_mb_frac(s_mem_overlay_snapshot.total_sdram);

    if (total_used_frac >= 10u) {
        total_used_frac = 0u;
        ++total_used_whole;
    }
    if (total_frac >= 10u) {
        total_frac = 0u;
        ++total_whole;
    }

    xhgc_mem_overlay_format_pair(app_line,
                                 sizeof(app_line),
                                 "APP",
                                 s_mem_overlay_snapshot.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used,
                                 s_mem_overlay_snapshot.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].peak);
    xhgc_mem_overlay_format_pair(dma_line,
                                 sizeof(dma_line),
                                 "DMA",
                                 s_mem_overlay_snapshot.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used,
                                 s_mem_overlay_snapshot.zone_stats[XHGC_MEM_ZONE_DMA_POOL].peak);
    xhgc_mem_overlay_format_pair(lua_line,
                                 sizeof(lua_line),
                                 "LUA",
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_LUA].used,
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_LUA].peak);
    xhgc_mem_overlay_format_pair(res_line,
                                 sizeof(res_line),
                                 "RES",
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_RESOURCE].used,
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_RESOURCE].peak);
    xhgc_mem_overlay_format_pair(lvgl_line,
                                 sizeof(lvgl_line),
                                 "LVG",
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_LVGL].used,
                                 s_mem_overlay_snapshot.tag_stats[XHGC_MEM_TAG_LVGL].peak);

    (void)snprintf(s_mem_overlay_text,
                   sizeof(s_mem_overlay_text),
                   "MEM %lu.%lu/%lu.%luMB\n"
                   "%s\n"
                   "%s\n"
                   "%s\n"
                   "%s\n"
                   "%s\n"
                   "FAIL %lu",
                   total_used_whole,
                   total_used_frac,
                   total_whole,
                   total_frac,
                   app_line,
                   dma_line,
                   lua_line,
                   res_line,
                   lvgl_line,
                   (unsigned long)s_mem_overlay_snapshot.total_fail_count);

    lv_label_set_text_static(s_mem_overlay_label, s_mem_overlay_text);
}
