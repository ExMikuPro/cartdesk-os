#ifndef DISPLAY_MODE_H
#define DISPLAY_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 显示模式状态机。
 *
 *   LVGL_APP             HW Layer0 disabled；HW Layer1 = 800x480 全屏 LVGL
 *                        DIRECT 双缓冲（Lua Cart / 普通 App）
 *   TO_LAUNCHER_HW_PAN   正在构造 Launcher cache 并准备 atomic latch
 *   LAUNCHER_HW_PAN      HW Layer0 = 固定 UI；HW Layer1 = strip 窗口 + 硬件平移
 *   TO_LVGL_APP          第一张 Lua full frame 已就绪，等待 atomic latch
 *
 * 切换必须是原子的：任何中间态都不允许被面板看到（不能出现半套 layer 配置、
 * 旧 scanout buffer 或几何错乱的一帧）。因此模式迁移被拆成
 * request -> commit 两步，且 request 会被 pending reload / 非 stable 状态拒绝。
 *
 * 本模块是纯逻辑，可在 host 上测试。
 */

typedef enum {
    DISPLAY_MODE_LVGL_APP = 0,
    DISPLAY_MODE_TO_LAUNCHER_HW_PAN,
    DISPLAY_MODE_LAUNCHER_HW_PAN,
    DISPLAY_MODE_TO_LVGL_APP,
    DISPLAY_MODE_COUNT
} display_mode_t;

typedef struct {
    display_mode_t mode;
    uint32_t request_count;
    uint32_t commit_count;
    uint32_t abort_count;
    uint32_t rejected_busy;        /* 已有切换在飞行中 */
    uint32_t rejected_reload;      /* 仍有未完成的 VBR */
    uint32_t rejected_invalid;     /* 非法目标 */
} display_mode_state_t;

/** 复位到 LVGL_APP 稳定态 */
void display_mode_init(display_mode_state_t *st);

/** 稳定态 = 不处于任何 TO_* 中间态 */
bool display_mode_is_stable(const display_mode_state_t *st);

/** LVGL 是否可以正常刷新（只有 app 模式与其进入过渡态允许） */
bool display_mode_lvgl_refresh_allowed(const display_mode_state_t *st);

/** Launcher 硬件平移是否正在驱动 HW Layer1 */
bool display_mode_hw_pan_active(const display_mode_state_t *st);

/** 中间态的目标模式；稳定态返回当前模式 */
display_mode_t display_mode_target(const display_mode_state_t *st);

/**
 * @brief  请求切换到一个稳定目标模式
 * @param  target          只能是 LVGL_APP 或 LAUNCHER_HW_PAN
 * @param  reload_pending  当前是否仍有未完成的 LTDC reload
 * @retval true=已进入对应 TO_* 中间态
 * @note   非 stable 或 reload_pending 时拒绝，调用方需要下一 tick 重试
 */
bool display_mode_request(display_mode_state_t *st,
                          display_mode_t target,
                          bool reload_pending);

/** 确认 latch 完成，进入目标稳定态；非中间态时返回 false */
bool display_mode_commit(display_mode_state_t *st);

/** 放弃当前切换并回到 from 稳定态；非中间态时返回 false */
bool display_mode_abort(display_mode_state_t *st, display_mode_t from);

/** 模式名字（trace/dump 用） */
const char *display_mode_name(display_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MODE_H */
