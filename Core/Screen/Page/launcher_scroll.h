// launcher_scroll.h
// Launcher 逻辑滚动控制器：拖动 / 边界 / 惯性 / 吸附。
//
// 该模块不依赖 LVGL、不操作任何对象，只维护 Launcher 私有的逻辑滚动位置
// logical_scroll_x 并把它交给调用方落地。因此同一份实现既跑在目标板上，也能
// 在 host 侧用确定性测试直接驱动（见 tests/host/launcher_scroll_test.c）。
//
// 交互语义刻意对齐 native LVGL scroll（vendored LVGL 9.6.0 默认值）：
//   - 超过 LAUNCHER_SCROLL_DIRECTION_LIMIT 的位移才认定为滚动，
//     且越过阈值的那一个采样被阈值检测吸收（与 lv_indev_find_scroll_obj 一致）；
//   - 越界时按弹性系数收缩（与 lv_indev_scroll.c: elastic_diff 一致）；
//   - 甩动速度用「基于真实 dt 的指数移动平均」估算，不依赖固定帧率；
//   - 松手后的衰减按真实经过时间计算，不假设固定帧率。
#ifndef LAUNCHER_SCROLL_H
#define LAUNCHER_SCROLL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与 vendored LVGL 的 LV_INDEV_DEF_SCROLL_LIMIT 保持一致。 */
#ifndef LAUNCHER_SCROLL_DIRECTION_LIMIT
#define LAUNCHER_SCROLL_DIRECTION_LIMIT 10
#endif

/* 与 vendored LVGL 的 LV_INDEV_DEF_SCROLL_ELASTIC_FACTOR 保持一致。 */
#ifndef LAUNCHER_SCROLL_ELASTIC_FACTOR
#define LAUNCHER_SCROLL_ELASTIC_FACTOR 4
#endif

/* 速度估算的滑窗长度（采样个数）与最大采样间隔（ms）。超过窗口的采样不再
 * 代表「松手瞬间」的速度。REQ-17：不假设固定帧率。 */
#ifndef LAUNCHER_SCROLL_VELOCITY_WINDOW
#define LAUNCHER_SCROLL_VELOCITY_WINDOW 8
#endif
#ifndef LAUNCHER_SCROLL_VELOCITY_WINDOW_MS
#define LAUNCHER_SCROLL_VELOCITY_WINDOW_MS 255
#endif

/* 甩动惯性：每 100 ms 保留 90% 速度（与 LVGL 的 scroll_throw 默认档同义）。
 * 这里用定点指数衰减而不是 LVGL 的截断线性近似，保证基于真实 dt 且不产生
 * 亚像素拖尾。Q16 的 ln(0.9)/100。 */
#ifndef LAUNCHER_SCROLL_THROW_DECAY_PERCENT
#define LAUNCHER_SCROLL_THROW_DECAY_PERCENT 10
#endif
#ifndef LAUNCHER_SCROLL_THROW_STEP_MS
#define LAUNCHER_SCROLL_THROW_STEP_MS 100
#endif
#ifndef LAUNCHER_SCROLL_DECAY_Q16
#define LAUNCHER_SCROLL_DECAY_Q16 64975
#endif

/* 停止阈值：throw_vect 是「每 100 ms 窗口的剩余位移」。低于该值时残余速度
 * 在本阶段任何帧率下都不足 1 px，直接转入吸附，避免亚像素拖尾。 */
#ifndef LAUNCHER_SCROLL_STOP_THRESHOLD
#define LAUNCHER_SCROLL_STOP_THRESHOLD 16
#endif

/* 单次甩动的距离上限（px），与 LVGL 的 indev scroll_limit 同义：一次手势
 * 不能把内容甩到任意远处。 */
#ifndef LAUNCHER_SCROLL_MAX_THROW
#define LAUNCHER_SCROLL_MAX_THROW 500
#endif

/* 吸附动画时长（ms）。 */
#ifndef LAUNCHER_SCROLL_SNAP_MS
#define LAUNCHER_SCROLL_SNAP_MS 200
#endif

typedef enum {
    LAUNCHER_SCROLL_IDLE = 0,      /* 无手势、无惯性、无吸附 */
    LAUNCHER_SCROLL_DRAGGING = 1,  /* 手指按住并已越过滚动阈值 */
    LAUNCHER_SCROLL_INERTIA = 2,   /* 松手后的 throw 衰减 */
    LAUNCHER_SCROLL_SNAPPING = 3,  /* 吸附到最近 slot */
    LAUNCHER_SCROLL_PRESSED = 4,   /* 已按下但尚未越过滚动阈值 */
} launcher_scroll_state_t;

typedef struct {
    int32_t position;      /* logical_scroll_x，唯一权威滚动位置 */
    int32_t max_position;  /* content_width - viewport_width */
    int32_t snap_anchor_x; /* 吸附网格原点；网格 = {anchor + k*pitch} ∪ {0, max} */
    int32_t slot_pitch;    /* slot 间距 = BOX_WIDTH + BOX_SPACING */
    int32_t slot_count;
} launcher_scroll_config_t;

/* 需要 Launcher 把 logical_scroll_x 落地到对象树时调用。 */
typedef void (*launcher_scroll_apply_cb_t)(int32_t position, void *user_data);

typedef struct {
    launcher_scroll_config_t config;
    launcher_scroll_apply_cb_t apply_cb;
    void *user_data;

    int32_t position;
    int32_t throw_vect;         /* 剩余甩动距离，单位 px */
    int32_t drag_scroll_sum;    /* 用于阈值判定的累计位移 */
    int32_t snap_from;
    int32_t snap_to;

    launcher_scroll_state_t state;
    bool snap_active;
    bool press_was_scrolling;   /* 本次手势是否真的滚动过（用于抑制 click） */

    uint32_t snap_start_ms;
    uint32_t drag_last_ms;
    uint32_t last_tick;

    struct {
        int32_t delta;
        int32_t dt_ms;   /* 该采样代表的真实时间跨度 */
        uint32_t tick;   /* 采样结束时刻 */
    } vel_hist[LAUNCHER_SCROLL_VELOCITY_WINDOW];
    uint32_t vel_hist_index;
    uint32_t vel_last_ms;

    bool initialized;
    bool prev_pressed;
    int32_t prev_point_x;
} launcher_scroll_t;

/**
 * @brief 初始化/复位滚动控制器
 * @param cfg 几何配置，函数内部会拷贝
 * @param apply_cb 位置落地回调，可为 NULL
 */
void launcher_scroll_init(launcher_scroll_t *scroll,
                          const launcher_scroll_config_t *cfg,
                          launcher_scroll_apply_cb_t apply_cb,
                          void *user_data);

/** @brief 直接设置位置（越界会被夹紧），用于确定性基准与场景恢复。 */
void launcher_scroll_set_position(launcher_scroll_t *scroll, int32_t position);

/** @brief 停止惯性/吸附并回到空闲。 */
void launcher_scroll_cancel(launcher_scroll_t *scroll);

/**
 * @brief 推进一帧
 * @param pressed 当前指针是否按下
 * @param point_x 当前指针 x（未按下时忽略）
 * @param now_ms  单调毫秒时间戳（lv_tick_get() 或 HAL_GetTick()）
 * @return 位置是否发生变化
 */
bool launcher_scroll_tick(launcher_scroll_t *scroll, bool pressed,
                          int32_t point_x, uint32_t now_ms);

/** @brief 吸附网格上离 position 最近、且已按 [0, max] 夹紧的目标。 */
int32_t launcher_scroll_nearest_snap(const launcher_scroll_t *scroll, int32_t position);

/** @brief 最近 slot 索引（吸附目标的网格序号）。 */
int32_t launcher_scroll_nearest_index(const launcher_scroll_t *scroll, int32_t position);

/**
 * @brief 当前估算的甩动距离（px / 100 ms 窗口）。
 *
 * 生产路径在松手时内部直接使用同一估算；该访问器用于确定性回归测试与实机
 * 调试观察（例如验证同一手势在不同采样率下给出同一个速度）。
 */
int32_t launcher_scroll_estimate_throw(const launcher_scroll_t *scroll, uint32_t now_ms);

/** @brief 本次手势是否滚动过；调用后自动清除。 */
bool launcher_scroll_take_scroll_gesture(launcher_scroll_t *scroll);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_SCROLL_H */
