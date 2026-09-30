#ifndef LAUNCHER_DISPLAY_H
#define LAUNCHER_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "display_mode.h"
#include "launcher_hw_pan.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Launcher 双层显示编排：模式状态机 + 硬件平移 + 缓存渲染 + LTDC 配置。
 *
 * HW Layer0 = 固定 UI 单缓冲（LAYER0_FB 为 front，LVGL_FB_A 为 staging back）
 * HW Layer1 = Launcher strip 窗口（硬件平移）或 LVGL DIRECT 双缓冲
 *
 * 重要约束：
 *   - 任何切换都是一次 atomic VBR，中间态不允许被面板看到
 *   - Launcher 模式不产生 LVGL 刷新工作量（invalidation 关闭）
 *   - Lua 模式恢复后 LVGL 完全不知道 Launcher 用过硬件平移
 */

typedef struct {
    uintptr_t static_front_addr;   /* LAYER0_FB */
    uintptr_t static_back_addr;    /* LVGL_FB_A（Launcher 模式下 LVGL 不用 scanout） */
    uintptr_t strip_base_addr;     /* LAUNCHER_STRIP arena */
    uint32_t  strip_width;         /* 2660 */
    uint32_t  strip_height;        /* 350 */
    uint32_t  strip_stride_bytes;  /* 10656 */
    uint32_t  viewport_width;      /* 800 */
    uint32_t  window_y;            /* strip 窗口在面板上的起始行 */
    uint32_t  window_height;       /* strip 窗口高度（同时是 CFBLNR） */
} launcher_display_geometry_t;

typedef enum {
    LAUNCHER_FALLBACK_NONE = 0,
    LAUNCHER_FALLBACK_DISABLED,          /* 编译期开关关闭 */
    LAUNCHER_FALLBACK_GEOMETRY,          /* UI 几何不满足双图层前提 */
    LAUNCHER_FALLBACK_CACHE_BUILD,       /* strip / static 渲染失败 */
    LAUNCHER_FALLBACK_ICON_TIMEOUT,      /* icon 未 READY 超时 */
    LAUNCHER_FALLBACK_BUSY,              /* 切换被占用（重试中） */
    LAUNCHER_FALLBACK_LTDC_CONFIG,       /* LTDC shadow 配置失败 */
} launcher_fallback_reason_t;

typedef struct {
    uint32_t mode_requests;
    uint32_t mode_commits;
    uint32_t mode_aborts;
    uint32_t strip_rebuilds;
    uint32_t static_rebuilds;
    uint32_t pan_set_x_calls;
    uint32_t layer0_swaps;
    uint32_t fallback_count;
    launcher_fallback_reason_t last_fallback;
    launcher_fallback_reason_t last_fallback_reason;
    bool hw_pan_active;
    bool lvgl_invalidation_disabled;
} launcher_display_stats_t;

/** 复位编排状态（启动时一次） */
void launcher_display_init(void);

/**
 * @brief  提供 UI 侧几何与对象，用于构造/重建缓存
 * @param  geo         SDRAM 与窗口几何（必须自洽）
 * @param  fixed_root  固定 UI 根对象（如 s_main_container）
 * @param  scroll_root 滚动内容根对象（如 box_container，渲染固定层时临时隐藏）
 * @param  content     滚动内容对象（如 content_container，渲染 strip 的源）
 * @retval true=几何通过校验，可以进入硬件平移模式
 */
bool launcher_display_configure(const launcher_display_geometry_t *geo,
                                lv_obj_t *fixed_root,
                                lv_obj_t *scroll_root,
                                lv_obj_t *content);

/** 数据（icon / app 名 / slot）是否已经 READY，可以烘焙缓存 */
typedef bool (*launcher_display_ready_fn)(void *user);
void launcher_display_set_ready_probe(launcher_display_ready_fn fn, void *user);

/** 标记滚动内容变化，需要重建 strip；滚动本身绝不能调用 */
void launcher_display_mark_strip_dirty(void);

/** 标记固定 UI 变化，需要重建 Layer0 静态层 */
void launcher_display_mark_static_dirty(void);

/** 请求进入 Launcher 硬件平移模式（异步，直到 commit 才生效） */
bool launcher_display_request_hw_pan(void);

/** 请求回到 LVGL app 模式（异步，第一张 Lua full frame 为 atomic latch） */
bool launcher_display_request_lvgl_app(void);

/** 每个 app task tick 调用一次，推进状态机 */
void launcher_display_tick(void);

/** 设置滚动位置（Launcher 模式 = 硬件平移；否则由 LVGL slot-local 路径处理） */
bool launcher_display_set_scroll_x(int32_t x);

/** ISR 分派入口：pan 完成 */
void launcher_display_on_pan_reload_complete(uint32_t generation);

/** ISR 分派入口：Layer0 static swap 完成 */
void launcher_display_on_layer0_swap_complete(uint32_t generation);

/** ISR 分派入口：mode switch 完成 */
void launcher_display_on_mode_switch_complete(uint32_t generation);

/** LVGL flush 前的钩子：TO_LVGL_APP 时把完整 Layer1 几何恢复到 shadow */
void launcher_display_on_lvgl_pre_latch(void *user);

/** 当前模式 / 是否硬件平移 */
display_mode_t launcher_display_mode(void);
bool launcher_display_hw_pan_active(void);

/** 最近一次 fallback 原因（NONE 表示没有 fallback） */
launcher_fallback_reason_t launcher_display_last_fallback(void);

const launcher_display_stats_t *launcher_display_stats(void);
const launcher_hw_pan_t *launcher_display_pan(void);

/*
 * GDB / 串口可读的运行期快照。
 *
 * 访问器函数体很小，会被内联掉，符号表里未必留下独立符号，因此这里维护一份
 * volatile 镜像供调试器直接读内存。字段是纯标量，不参与控制流，Release 下
 * 同样保留（体积极小），便于现场诊断硬件平移状态。
 */
typedef struct {
    uint32_t mode;
    uint32_t hw_pan_active;
    uint32_t fallback_reason;
    uint32_t lvgl_invalidation_disabled;
    int32_t  desired_x;
    int32_t  latched_x;
    int32_t  requested_x;
    uint32_t pan_pending;
    uint32_t pan_enabled;
    uint32_t pan_submits;
    uint32_t pan_coalesced;
    uint32_t pan_rejected;
    uint32_t pan_completions;
    uint32_t pan_generation_mismatch;
    uint32_t mode_requests;
    uint32_t mode_commits;
    uint32_t mode_aborts;
    uint32_t strip_rebuilds;
    uint32_t static_rebuilds;
    uint32_t strip_build_ms;
    uint32_t static_build_ms;
    uint32_t strip_build_failures;
    uint32_t static_build_failures;
    uint32_t cache_stride_check_failures;
    uint32_t fallback_count;
    /* LTDC 硬件寄存器镜像（每次 tick 采样） */
    uint32_t l0_cr;
    uint32_t l0_cfbar;
    uint32_t l0_cfblr;
    uint32_t l1_cr;
    uint32_t l1_cfbar;
    uint32_t l1_cfblr;
    uint32_t l1_cfblnr;
    uint32_t l1_whpcr;
    uint32_t l1_wvpcr;
    uint32_t srcr;
    uint32_t ltdc_isr;
    uint32_t vblank;
    /* reload owner 计数 */
    uint32_t reload_orphan;
    uint32_t reload_overrun;
    uint32_t reload_crosstalk;
    uint32_t reload_consistent;
    uint32_t owner_arms_lvgl;
    uint32_t owner_arms_pan;
    uint32_t owner_arms_layer0;
    uint32_t owner_arms_mode;
    uint32_t owner_done_lvgl;
    uint32_t owner_done_pan;
    uint32_t owner_done_layer0;
    uint32_t owner_done_mode;
} launcher_hwpan_debug_t;

extern volatile launcher_hwpan_debug_t g_launcher_hwpan_debug;

/** 立即刷新 GDB 快照（tick 与关键完成点都会调用） */
void launcher_display_refresh_debug(void);

/*
 * Deterministic GDB 控制口（沿用工程既有 mailbox 风格）。
 *
 *   g_launcher_hwpan_request_x : >=0 时把 scroll_x 设成该值并置回 -1；
 *                                GDB 用它做确定性的单步 pan 验证
 *   g_launcher_hwpan_stress_enable : 1 时每个 tick 自动 0->1860->0 往返，
 *                                pan 完成即推进，用于 10k 压力测试
 */
extern volatile int32_t  g_launcher_hwpan_request_x;
extern volatile uint32_t g_launcher_hwpan_stress_enable;
extern volatile uint32_t g_launcher_hwpan_stress_dir;
extern volatile uint32_t g_launcher_hwpan_stress_updates;
extern volatile int32_t  g_launcher_hwpan_stress_x;

/** 名字（日志/dump 用） */
const char *launcher_fallback_reason_name(launcher_fallback_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_DISPLAY_H */
