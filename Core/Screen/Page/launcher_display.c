#include "launcher_display.h"

#include <string.h>

#include "launcher_cache_render.h"
#include "lcd.h"
#include "ltdc_layer.h"
#include "ltdc_reload.h"
#include "lv_port_disp.h"
#include "sdram_layout.h"

/*
 * 编排状态机。
 *
 *   LVGL_APP --request--> TO_LAUNCHER_HW_PAN --commit--> LAUNCHER_HW_PAN
 *      ^                                                        |
 *      |                                                        |
 *      +-------- TO_LVGL_APP <------request----------------------+
 *
 * 每个箭头都对应一次 atomic VBR：
 *   - 进入 Launcher：configure L0 static + L1 strip window -> 关 invalidation
 *                     -> arm(MODE_SWITCH) -> VBR
 *   - 回到 Lua    ：重新开 invalidation + 全量失效 -> 第一张 full frame 的
 *                     LVGL flush 在同一个 VBR 内恢复 L1 全屏几何 + 关 L0
 */

typedef struct {
    bool cache_built;         /* strip + static 已经烘焙完成 */
    bool layers_configured;   /* shadow 已经写好，等待 arm */
    bool latch_armed;         /* 已发出 MODE_SWITCH VBR */
    uint32_t latch_generation;
    bool lvgl_flush_seen;     /* 回 Lua 时已经看到第一张 full frame flush */
} transition_state_t;

static display_mode_state_t s_mode;
static launcher_hw_pan_t s_pan;
static launcher_display_geometry_t s_geo;
static launcher_display_stats_t s_stats;
static transition_state_t s_trans;

static lv_obj_t *s_fixed_root;
static lv_obj_t *s_scroll_root;
static lv_obj_t *s_content;
static launcher_display_ready_fn s_ready_fn;
static void *s_ready_user;

static bool s_strip_dirty = true;
static bool s_static_dirty = true;

static uintptr_t s_static_front;
static uintptr_t s_static_back;
static bool s_layer0_swap_pending;
static uint32_t s_layer0_swap_generation;

static bool s_configured;

#if PERF_MONITOR_ENABLE
volatile launcher_hwpan_debug_t g_launcher_hwpan_debug;

volatile int32_t  g_launcher_hwpan_request_x = -1;
volatile uint32_t g_launcher_hwpan_stress_enable;
volatile uint32_t g_launcher_hwpan_stress_dir;
volatile uint32_t g_launcher_hwpan_stress_updates;
volatile int32_t  g_launcher_hwpan_stress_x;

static bool launcher_display_set_scroll_x_impl(int32_t x);

/* 确定性 GDB 驱动：先处理显式单步位置，再处理 10k 压力往返。 */
static void launcher_display_service_debug_hooks(void)
{
    if (!display_mode_hw_pan_active(&s_mode)) {
        return;
    }

    if (g_launcher_hwpan_request_x >= 0) {
        const int32_t x = g_launcher_hwpan_request_x;
        g_launcher_hwpan_request_x = -1;
        (void)launcher_display_set_scroll_x_impl(x);
        g_launcher_hwpan_stress_x = x;
        return;
    }

    if (g_launcher_hwpan_stress_enable == 0u) {
        return;
    }

    /* 只在上一笔 pan 已落地时推进：保证"最大 presentation cadence"而不是
     * 把请求堆在 pending 上。 */
    if (s_pan.pending) {
        return;
    }

    int32_t x = g_launcher_hwpan_stress_x;
    if (g_launcher_hwpan_stress_dir == 0u) {
        x += 4;
        if (x >= (int32_t)s_pan.geo.scroll_max) {
            x = (int32_t)s_pan.geo.scroll_max;
            g_launcher_hwpan_stress_dir = 1u;
        }
    } else {
        x -= 4;
        if (x <= 0) {
            x = 0;
            g_launcher_hwpan_stress_dir = 0u;
        }
    }

    g_launcher_hwpan_stress_x = x;
    if (launcher_display_set_scroll_x_impl(x)) {
        ++g_launcher_hwpan_stress_updates;
    }
}
void launcher_display_refresh_debug(void)
{
    const ltdc_reload_state_t *rl = ltdc_reload_state();
    const launcher_cache_stats_t *cache = launcher_cache_render_stats();

    g_launcher_hwpan_debug.mode = (uint32_t)s_mode.mode;
    g_launcher_hwpan_debug.hw_pan_active = display_mode_hw_pan_active(&s_mode) ? 1u : 0u;
    g_launcher_hwpan_debug.fallback_reason = (uint32_t)s_stats.last_fallback_reason;
    g_launcher_hwpan_debug.lvgl_invalidation_disabled =
        s_stats.lvgl_invalidation_disabled ? 1u : 0u;

    g_launcher_hwpan_debug.desired_x = s_pan.desired_x;
    g_launcher_hwpan_debug.latched_x = s_pan.latched_x;
    g_launcher_hwpan_debug.requested_x = s_pan.requested_x;
    g_launcher_hwpan_debug.pan_pending = s_pan.pending ? 1u : 0u;
    g_launcher_hwpan_debug.pan_enabled = s_pan.enabled ? 1u : 0u;
    g_launcher_hwpan_debug.pan_submits = s_pan.submits;
    g_launcher_hwpan_debug.pan_coalesced = s_pan.coalesced;
    g_launcher_hwpan_debug.pan_rejected = s_pan.vbr_rejected;
    g_launcher_hwpan_debug.pan_completions = s_pan.completions;
    g_launcher_hwpan_debug.pan_generation_mismatch = s_pan.generation_mismatch;

    g_launcher_hwpan_debug.mode_requests = s_stats.mode_requests;
    g_launcher_hwpan_debug.mode_commits = s_stats.mode_commits;
    g_launcher_hwpan_debug.mode_aborts = s_stats.mode_aborts;
    g_launcher_hwpan_debug.commit_to_lvgl_app = s_stats.commit_to_lvgl_app;
    g_launcher_hwpan_debug.commit_to_hw_pan = s_stats.commit_to_hw_pan;
    g_launcher_hwpan_debug.dm_rejected_busy = s_mode.rejected_busy;
    g_launcher_hwpan_debug.dm_rejected_reload = s_mode.rejected_reload;
    g_launcher_hwpan_debug.dm_rejected_invalid = s_mode.rejected_invalid;
    g_launcher_hwpan_debug.flush_idle = lv_port_disp_is_flush_idle() ? 1u : 0u;
    g_launcher_hwpan_debug.strip_rebuilds = s_stats.strip_rebuilds;
    g_launcher_hwpan_debug.static_rebuilds = s_stats.static_rebuilds;
    g_launcher_hwpan_debug.fallback_count = s_stats.fallback_count;

    if (cache != NULL) {
        g_launcher_hwpan_debug.strip_build_ms = cache->last_strip_build_ms;
        g_launcher_hwpan_debug.static_build_ms = cache->last_static_build_ms;
        g_launcher_hwpan_debug.strip_build_failures = cache->strip_failures;
        g_launcher_hwpan_debug.static_build_failures = cache->static_failures;
        g_launcher_hwpan_debug.cache_stride_check_failures = cache->stride_check_failures;
    }

    Lcd_LtdcRegisterSnapshot snap;
    Lcd_LtdcSnapshotRegisters(&snap);
    g_launcher_hwpan_debug.l0_cr = snap.layer0_cr;
    g_launcher_hwpan_debug.l0_cfbar = snap.layer0_cfbar;
    g_launcher_hwpan_debug.l0_cfblr = snap.layer0_cfblr;
    g_launcher_hwpan_debug.l1_cr = snap.layer1_cr;
    g_launcher_hwpan_debug.l1_cfbar = snap.layer1_cfbar;
    g_launcher_hwpan_debug.l1_cfblr = snap.layer1_cfblr;
    g_launcher_hwpan_debug.l1_cfblnr = snap.layer1_cfblnr;
    g_launcher_hwpan_debug.l1_whpcr = snap.layer1_whpcr;
    g_launcher_hwpan_debug.l1_wvpcr = snap.layer1_wvpcr;
    g_launcher_hwpan_debug.srcr = snap.srcr;
    g_launcher_hwpan_debug.ltdc_isr = snap.isr;
    g_launcher_hwpan_debug.vblank = LCD_GetVBlankCount();

    if (rl != NULL) {
        g_launcher_hwpan_debug.reload_pending_owner = (uint32_t)rl->pending_owner;
        g_launcher_hwpan_debug.reload_orphan = rl->orphan_completions;
        g_launcher_hwpan_debug.reload_overrun = rl->overrun_count;
        g_launcher_hwpan_debug.reload_crosstalk = rl->crosstalk_count;
        g_launcher_hwpan_debug.reload_consistent = ltdc_reload_is_consistent() ? 1u : 0u;
        g_launcher_hwpan_debug.owner_arms_lvgl = rl->owner_arms[LTDC_RELOAD_OWNER_LVGL_FLUSH];
        g_launcher_hwpan_debug.owner_arms_pan = rl->owner_arms[LTDC_RELOAD_OWNER_LAUNCHER_PAN];
        g_launcher_hwpan_debug.owner_arms_layer0 = rl->owner_arms[LTDC_RELOAD_OWNER_LAYER0_SWAP];
        g_launcher_hwpan_debug.owner_arms_mode = rl->owner_arms[LTDC_RELOAD_OWNER_MODE_SWITCH];
        g_launcher_hwpan_debug.owner_done_lvgl = rl->owner_completions[LTDC_RELOAD_OWNER_LVGL_FLUSH];
        g_launcher_hwpan_debug.owner_done_pan = rl->owner_completions[LTDC_RELOAD_OWNER_LAUNCHER_PAN];
        g_launcher_hwpan_debug.owner_done_layer0 = rl->owner_completions[LTDC_RELOAD_OWNER_LAYER0_SWAP];
        g_launcher_hwpan_debug.owner_done_mode = rl->owner_completions[LTDC_RELOAD_OWNER_MODE_SWITCH];
    }
}

#endif /* PERF_MONITOR_ENABLE */

/* ---------------------------------------------------------------- pan ops */

static void pan_write_source_addr(uintptr_t addr, void *user)
{
    (void)user;
    (void)Lcd_LtdcSetSourceAddr(HW_L1_CONTENT_LAYER_IDX, addr);
}

static bool pan_request_vbr(void *user)
{
    (void)user;
    return Lcd_LtdcRequestVblankReload();
}

/* -------------------------------------------------------------- ISR 分派 */

static bool launcher_display_presentation_gate(void *user)
{
    (void)user;
    /* 硬件平移模式下 LVGL 绝不拥有 HW Layer1。 */
    return !display_mode_hw_pan_active(&s_mode);
}

static void launcher_display_external_dispatch(ltdc_reload_owner_t owner,
                                               uint32_t generation,
                                               void *user)
{
    (void)user;

    switch (owner) {
    case LTDC_RELOAD_OWNER_LAUNCHER_PAN:
        launcher_display_on_pan_reload_complete(generation);
        break;
    case LTDC_RELOAD_OWNER_LAYER0_SWAP:
        launcher_display_on_layer0_swap_complete(generation);
        break;
    case LTDC_RELOAD_OWNER_MODE_SWITCH:
        launcher_display_on_mode_switch_complete(generation);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ init */

static bool s_initialized;

void launcher_display_init(void)
{
    /* Launcher 每次返回都会重建对象树并重新 configure；累计计数与 fallback
     * 历史必须跨重建保留，否则模式切换压力测试无法统计。只有首次初始化清零。 */
    if (!s_initialized) {
        (void)memset(&s_stats, 0, sizeof(s_stats));
        s_initialized = true;
    }

    (void)memset(&s_trans, 0, sizeof(s_trans));

    display_mode_init(&s_mode);

    /* 几何先置零；configure() 成功后才可用于硬件平移。 */
    (void)memset(&s_geo, 0, sizeof(s_geo));

    launcher_hw_pan_ops_t ops = {
        .write_source_addr = pan_write_source_addr,
        .request_vbr = pan_request_vbr,
        .user = NULL,
    };
    launcher_hw_pan_init(&s_pan, NULL, &ops, 0);

    s_fixed_root = NULL;
    s_scroll_root = NULL;
    s_content = NULL;
    s_ready_fn = NULL;
    s_ready_user = NULL;
    s_strip_dirty = true;
    s_static_dirty = true;
    s_layer0_swap_pending = false;
    s_layer0_swap_generation = 0u;
    s_configured = false;

    ltdc_reload_set_external_dispatch(launcher_display_external_dispatch, NULL);
    lv_port_disp_set_pre_latch_hook(launcher_display_on_lvgl_pre_latch, NULL);
    lv_port_disp_set_presentation_gate(launcher_display_presentation_gate, NULL);
}

/* ------------------------------------------------------------- configure */

bool launcher_display_configure(const launcher_display_geometry_t *geo,
                                lv_obj_t *fixed_root,
                                lv_obj_t *scroll_root,
                                lv_obj_t *content)
{
    if (geo == NULL || fixed_root == NULL || content == NULL) {
        s_stats.last_fallback = LAUNCHER_FALLBACK_GEOMETRY;
        return false;
    }

    launcher_hw_pan_geometry_t pan_geo = {
        .base_addr = geo->strip_base_addr,
        .stride_bytes = geo->strip_stride_bytes,
        .width = geo->strip_width,
        .viewport_width = geo->viewport_width,
        .scroll_max = geo->strip_width - geo->viewport_width,
    };

    if (!launcher_hw_pan_geometry_is_valid(&pan_geo)) {
        s_stats.last_fallback = LAUNCHER_FALLBACK_GEOMETRY;
        return false;
    }

    if (geo->window_height == 0u ||
        (geo->window_y + geo->window_height) > (uint32_t)LCD_H) {
        s_stats.last_fallback = LAUNCHER_FALLBACK_GEOMETRY;
        return false;
    }

    s_geo = *geo;
    s_fixed_root = fixed_root;
    s_scroll_root = scroll_root;
    s_content = content;
    s_static_front = geo->static_front_addr;
    s_static_back = geo->static_back_addr;

    launcher_hw_pan_ops_t ops = {
        .write_source_addr = pan_write_source_addr,
        .request_vbr = pan_request_vbr,
        .user = NULL,
    };
    launcher_hw_pan_init(&s_pan, &pan_geo, &ops, 0);

    s_configured = true;
    s_strip_dirty = true;
    s_static_dirty = true;
    return true;
}

void launcher_display_set_ready_probe(launcher_display_ready_fn fn, void *user)
{
    s_ready_fn = fn;
    s_ready_user = user;
}

/* ---------------------------------------------------------- cache 烘焙 */

static bool launcher_display_data_ready(void)
{
    if (s_ready_fn == NULL) {
        return true;
    }
    return s_ready_fn(s_ready_user);
}

static bool launcher_display_build_caches(void)
{
    bool ok = true;

    if (s_strip_dirty) {
        const launcher_cache_result_t r =
            launcher_cache_render_strip(s_content,
                                        s_geo.strip_base_addr,
                                        s_geo.strip_width,
                                        s_geo.strip_height);
        if (r != LAUNCHER_CACHE_OK) {
            s_stats.last_fallback_reason = LAUNCHER_FALLBACK_CACHE_BUILD;
            return false;
        }
        s_strip_dirty = false;
        ++s_stats.strip_rebuilds;
    }

    if (s_static_dirty) {
        /* 固定层双缓冲：写 staging back，再由 Layer0 CFBAR swap 呈现。 */
        const launcher_cache_result_t r =
            launcher_cache_render_static(s_fixed_root,
                                         s_scroll_root,
                                         s_static_back,
                                         (uint32_t)SDRAM_LAYER0_FB_WIDTH,
                                         (uint32_t)SDRAM_LAYER0_FB_HEIGHT);
        if (r != LAUNCHER_CACHE_OK) {
            s_stats.last_fallback_reason = LAUNCHER_FALLBACK_CACHE_BUILD;
            return false;
        }
        s_static_dirty = false;
        ++s_stats.static_rebuilds;
    }

    (void)ok;
    return true;
}

/* -------------------------------------------------------------- 进入 pan */

static void launcher_display_enter_step(void)
{
    if (!s_configured) {
        ++s_stats.fallback_count;
        s_stats.last_fallback_reason = LAUNCHER_FALLBACK_GEOMETRY;
        (void)display_mode_abort(&s_mode, DISPLAY_MODE_LVGL_APP);
        return;
    }

    /* 等 icon / app 名 / slot 全部 READY 再烘焙，避免把 LOADING 占位图写进 strip */
    if (!launcher_display_data_ready()) {
        s_stats.last_fallback_reason = LAUNCHER_FALLBACK_ICON_TIMEOUT;
        return; /* 下一 tick 继续等待 */
    }

    if (!s_trans.cache_built) {
        if (!launcher_display_build_caches()) {
            ++s_stats.fallback_count;
            s_stats.last_fallback = s_stats.last_fallback_reason;
            (void)display_mode_abort(&s_mode, DISPLAY_MODE_LVGL_APP);
            return;
        }
        s_trans.cache_built = true;
    }

    if (!s_trans.layers_configured) {
        if (!Lcd_LtdcConfigureLayer0Static(s_static_back) ||
            !Lcd_LtdcConfigureStripWindow(s_geo.strip_base_addr,
                                          s_geo.window_y,
                                          s_geo.window_height,
                                          s_geo.strip_stride_bytes / 4u,
                                          s_geo.viewport_width)) {
            ++s_stats.fallback_count;
            s_stats.last_fallback_reason = LAUNCHER_FALLBACK_LTDC_CONFIG;
            (void)display_mode_abort(&s_mode, DISPLAY_MODE_LVGL_APP);
            return;
        }
        s_static_front = s_static_back;
        s_trans.layers_configured = true;
    }

    if (!s_trans.latch_armed) {
        /* Launcher 模式不再需要 LVGL 刷新：关闭 invalidation 但保留对象/输入。 */
        lv_display_t *disp = lv_display_get_default();
        if (disp != NULL) {
            lv_display_enable_invalidation(disp, false);
            s_stats.lvgl_invalidation_disabled = true;
        }

        const uint32_t gen = ltdc_reload_arm(LTDC_RELOAD_OWNER_MODE_SWITCH);
        if (gen == 0u || !Lcd_LtdcRequestVblankReload()) {
            if (gen != 0u) {
                (void)ltdc_reload_abandon(LTDC_RELOAD_OWNER_MODE_SWITCH);
            }
            if (disp != NULL) {
                lv_display_enable_invalidation(disp, true);
                s_stats.lvgl_invalidation_disabled = false;
            }
            ++s_stats.fallback_count;
            s_stats.last_fallback_reason = LAUNCHER_FALLBACK_BUSY;
            (void)display_mode_abort(&s_mode, DISPLAY_MODE_LVGL_APP);
            return;
        }

        s_trans.latch_generation = gen;
        s_trans.latch_armed = true;
    }

    /* 等待 on_mode_switch_complete() 提交模式 */
}

/* -------------------------------------------------------------- 回到 Lua */

static void launcher_display_leave_step(void)
{
    /* 1. 停止新的 pan，并等最后一笔 pan VBR 落地 */
    if (!s_trans.cache_built) {
        launcher_hw_pan_set_enabled(&s_pan, false);
        if (s_pan.pending) {
            return; /* 等 pan 完成 */
        }
        s_trans.cache_built = true;
    }

    /* 2. 重新允许 LVGL 失效，并强制全量重绘：Lua 会画第一张 full frame */
    if (!s_trans.layers_configured) {
        lv_display_t *disp = lv_display_get_default();
        if (disp != NULL) {
            lv_display_enable_invalidation(disp, true);
            s_stats.lvgl_invalidation_disabled = false;
        }
        if (s_fixed_root != NULL) {
            lv_obj_invalidate(s_fixed_root);
        }
        s_trans.layers_configured = true;
    }

    /* 3. 第一张 full frame 的 LVGL flush 会通过 pre-latch 钩子在同一次 VBR 内
     *    恢复 HW Layer1 全屏几何并关闭 HW Layer0；这里等它落地。 */
    if (s_trans.lvgl_flush_seen && lv_port_disp_is_flush_idle()) {
        (void)display_mode_commit(&s_mode);
        ++s_stats.mode_commits;
        ++s_stats.commit_to_lvgl_app;
        (void)memset(&s_trans, 0, sizeof(s_trans));
        s_stats.hw_pan_active = false;
    }
}

void launcher_display_tick(void)
{
    switch (s_mode.mode) {
    case DISPLAY_MODE_TO_LAUNCHER_HW_PAN:
        launcher_display_enter_step();
        break;
    case DISPLAY_MODE_TO_LVGL_APP:
        launcher_display_leave_step();
        break;
    case DISPLAY_MODE_LAUNCHER_HW_PAN:
        /* steady state：提交尚未生效的最新位置。strip/static 仍在 dirty 时
         * 原地改缓存会撕裂正在扫描的 surface，因此跳过提交（低频 rebuild 由
         * 显式接口在进入模式前完成）。 */
        if (!s_strip_dirty && !s_static_dirty) {
            (void)launcher_hw_pan_tick(&s_pan);
        }
        break;
    default:
        break;
    }

#if PERF_MONITOR_ENABLE
    launcher_display_service_debug_hooks();
    launcher_display_refresh_debug();
#endif
}

/* --------------------------------------------------------------- 请求入口 */

bool launcher_display_request_hw_pan(void)
{
#if !CARTDESK_LAUNCHER_HW_PAN
    ++s_stats.fallback_count;
    s_stats.last_fallback_reason = LAUNCHER_FALLBACK_DISABLED;
    return false;
#else
    if (!s_configured) {
        ++s_stats.fallback_count;
        s_stats.last_fallback_reason = LAUNCHER_FALLBACK_GEOMETRY;
        return false;
    }

    if (!display_mode_request(&s_mode, DISPLAY_MODE_LAUNCHER_HW_PAN,
                              lv_port_disp_is_flush_idle() == false)) {
        s_stats.last_fallback_reason = LAUNCHER_FALLBACK_BUSY;
        return false;
    }

    ++s_stats.mode_requests;
    (void)memset(&s_trans, 0, sizeof(s_trans));
    return true;
#endif
}

bool launcher_display_request_lvgl_app(void)
{
    if (!display_mode_request(&s_mode, DISPLAY_MODE_LVGL_APP,
                              lv_port_disp_is_flush_idle() == false)) {
        s_stats.last_fallback_reason = LAUNCHER_FALLBACK_BUSY;
        return false;
    }

    ++s_stats.mode_requests;
    (void)memset(&s_trans, 0, sizeof(s_trans));
    return true;
}

/* ------------------------------------------------------------------ 滚动 */

static bool launcher_display_set_scroll_x_impl(int32_t x)
{
    ++s_stats.pan_set_x_calls;

    if (!display_mode_hw_pan_active(&s_mode)) {
        return false;
    }

    return launcher_hw_pan_set_x(&s_pan, x);
}

bool launcher_display_set_scroll_x(int32_t x)
{
    return launcher_display_set_scroll_x_impl(x);
}

/* ------------------------------------------------------------- ISR 回调 */

void launcher_display_on_pan_reload_complete(uint32_t generation)
{
    launcher_hw_pan_on_reload_complete(&s_pan, generation);
}

void launcher_display_on_layer0_swap_complete(uint32_t generation)
{
    if (!s_layer0_swap_pending || generation != s_layer0_swap_generation) {
        return;
    }

    const uintptr_t tmp = s_static_front;
    s_static_front = s_static_back;
    s_static_back = tmp;
    s_layer0_swap_pending = false;
    ++s_stats.layer0_swaps;
}

void launcher_display_on_mode_switch_complete(uint32_t generation)
{
    if (!s_trans.latch_armed || generation != s_trans.latch_generation) {
        return;
    }

    s_trans.latch_armed = false;

    if (s_mode.mode == DISPLAY_MODE_TO_LAUNCHER_HW_PAN) {
        (void)display_mode_commit(&s_mode);
        ++s_stats.mode_commits;
        ++s_stats.commit_to_hw_pan;
        launcher_hw_pan_set_enabled(&s_pan, true);
        launcher_hw_pan_set_x(&s_pan, s_pan.desired_x);
        s_stats.hw_pan_active = true;
        (void)memset(&s_trans, 0, sizeof(s_trans));
        s_trans.cache_built = true;
        s_trans.layers_configured = true;
    }
}

void launcher_display_on_lvgl_pre_latch(void *user)
{
    (void)user;
    /* 只在本模式的过渡里动图层几何；稳定 LVGL_APP 下必须完全无副作用。 */
    if (s_mode.mode != DISPLAY_MODE_TO_LVGL_APP) {
        return;
    }

    (void)Lcd_LtdcConfigureLvglFullscreen(s_static_front);
    Lcd_LtdcSetLayerEnabled(HW_L0_STATIC_LAYER_IDX, false);
    s_trans.lvgl_flush_seen = true;
}

/* ------------------------------------------------------------- dirty 标记 */

void launcher_display_mark_strip_dirty(void)
{
    s_strip_dirty = true;
}

void launcher_display_mark_static_dirty(void)
{
    s_static_dirty = true;
}

/* ------------------------------------------------------------------ 查询 */

display_mode_t launcher_display_mode(void)
{
    return s_mode.mode;
}

bool launcher_display_hw_pan_active(void)
{
    return display_mode_hw_pan_active(&s_mode);
}

launcher_fallback_reason_t launcher_display_last_fallback(void)
{
    return s_stats.last_fallback;
}

const launcher_display_stats_t *launcher_display_stats(void)
{
    return &s_stats;
}

const launcher_hw_pan_t *launcher_display_pan(void)
{
    return &s_pan;
}

const char *launcher_fallback_reason_name(launcher_fallback_reason_t reason)
{
    switch (reason) {
    case LAUNCHER_FALLBACK_NONE:           return "NONE";
    case LAUNCHER_FALLBACK_DISABLED:       return "DISABLED";
    case LAUNCHER_FALLBACK_GEOMETRY:       return "GEOMETRY";
    case LAUNCHER_FALLBACK_CACHE_BUILD:    return "CACHE_BUILD";
    case LAUNCHER_FALLBACK_ICON_TIMEOUT:   return "ICON_TIMEOUT";
    case LAUNCHER_FALLBACK_BUSY:           return "BUSY";
    case LAUNCHER_FALLBACK_LTDC_CONFIG:    return "LTDC_CONFIG";
    default:                               return "?";
    }
}
