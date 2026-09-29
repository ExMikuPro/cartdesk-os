// launcher_scroll.c
// Launcher 逻辑滚动控制器实现。
//
// 见 launcher_scroll.h 的语义说明。所有更新都发生在调用 launcher_scroll_tick()
// 的线程（app/LVGL owner task）里；本模块不注册 timer、不接触 ISR。

#include "launcher_scroll.h"

#include <string.h>

/*
 * Debug-only 运动模式与 trace 计数。默认 CURRENT_MOTION，因此不改变生产行为。
 * 只有 Debug-LTDC-Sync-Trace / Debug-LTDC-Full-Render-Audit 会去写这些 mailbox。
 */
static uint32_t s_motion_mode = LAUNCHER_MOTION_MODE_CURRENT;
static volatile uint32_t s_motion_samples;
static volatile uint32_t s_motion_updates;

void launcher_scroll_set_motion_mode(uint32_t mode)
{
    s_motion_mode = (mode == LAUNCHER_MOTION_MODE_DIRECT_DRAG_ONLY)
                        ? LAUNCHER_MOTION_MODE_DIRECT_DRAG_ONLY
                        : LAUNCHER_MOTION_MODE_CURRENT;
}

uint32_t launcher_scroll_get_motion_mode(void)
{
    return s_motion_mode;
}

void launcher_scroll_get_motion_counts(uint32_t *samples, uint32_t *updates)
{
    if(samples != NULL) {
        *samples = s_motion_samples;
    }
    if(updates != NULL) {
        *updates = s_motion_updates;
    }
}

static bool prv_direct_drag_active(void)
{
    return s_motion_mode == LAUNCHER_MOTION_MODE_DIRECT_DRAG_ONLY;
}


/* 吸附时每次 tick 的最大过渡步长不限制；动画在保持整数位置的前提下按
 * ease-out 曲线推进。 */

static int32_t prv_clamp_position(const launcher_scroll_t *scroll, int32_t value)
{
    if(value < 0) {
        return 0;
    }
    if(value > scroll->config.max_position) {
        return scroll->config.max_position;
    }
    return value;
}

static void prv_apply(launcher_scroll_t *scroll, int32_t position)
{
    const int32_t clamped = prv_clamp_position(scroll, position);

    if(clamped == scroll->position) {
        return;
    }

    /*
     * 纵深防御：DIRECT_DRAG_ONLY 下唯一允许的位移来源是手指拖动。若仍有任何
     * 自动路径试图写位置，这里直接拒绝，MODE 1 绝不可能出现自发运动。
     */
    if(prv_direct_drag_active() &&
       scroll->state != LAUNCHER_SCROLL_DRAGGING) {
        return;
    }

    scroll->position = clamped;
    ++s_motion_updates;
    if(scroll->apply_cb != NULL) {
        scroll->apply_cb(clamped, scroll->user_data);
    }
}

/*
 * 复刻 lv_indev_scroll.c: elastic_diff() 在 snap == LV_SCROLL_SNAP_NONE 且已越界
 * 时的行为：diff 先向 0 收 factor/2，再整体除以 factor。
 */
static int32_t prv_bound_delta(const launcher_scroll_t *scroll, int32_t delta)
{
    if(delta == 0) {
        return 0;
    }

    const int32_t current = scroll->position;
    const int32_t limit = (delta > 0) ? scroll->config.max_position : 0;

    if(current == limit) {
        if(delta > 0) {
            delta -= LAUNCHER_SCROLL_ELASTIC_FACTOR / 2;
        }
        else {
            delta += LAUNCHER_SCROLL_ELASTIC_FACTOR / 2;
        }
        delta /= LAUNCHER_SCROLL_ELASTIC_FACTOR;
    }
    else if((delta > 0 && current + delta > limit) ||
            (delta < 0 && current + delta < limit)) {
        /* 本帧正好撞到边界，只有越界部分按弹性速率走。 */
        const int32_t to_limit = limit - current;
        int32_t overshoot = delta - to_limit;

        if(overshoot > 0) {
            overshoot -= LAUNCHER_SCROLL_ELASTIC_FACTOR / 2;
        }
        else {
            overshoot += LAUNCHER_SCROLL_ELASTIC_FACTOR / 2;
        }
        delta = to_limit + overshoot / LAUNCHER_SCROLL_ELASTIC_FACTOR;
    }

    return delta;
}

/*
 * 惯性衰减：exp(-k*t)，k = -ln(0.9)/100 ms，即每 100 ms 保留 90% 速度。
 *
 * LVGL 的 indev_scroll_throw_decay() 在真实帧长下几乎不衰减，会把甩动尾巴
 * 拖到几百毫秒并产生大量亚像素步进。指数形式与 dt 成比例、帧率无关、可测。
 *
 * 实现用 Q16 定点幂运算 + 查表，避免热路径里的浮点和逐毫秒循环。
 */
#define LAUNCHER_SCROLL_DECAY_TABLE_STEPS 256
#define LAUNCHER_SCROLL_DECAY_TABLE_MAX_MS 255

/* 单位步衰减因子（Q16）：exp(-k)，k = -ln(0.9)/100。 */
static int32_t prv_throw_decay_q16(int32_t q16, int32_t steps)
{
    while(steps > 0) {
        q16 = (int32_t)(((int64_t)q16 * LAUNCHER_SCROLL_DECAY_Q16) >> 16);
        if(q16 == 0) {
            return 0;
        }
        --steps;
    }
    return q16;
}

static int32_t s_throw_decay_table[LAUNCHER_SCROLL_DECAY_TABLE_STEPS];
static bool s_throw_decay_table_ready;

static void prv_throw_decay_table_init(void)
{
    for(int32_t steps = 0; steps < LAUNCHER_SCROLL_DECAY_TABLE_STEPS; steps++) {
        s_throw_decay_table[steps] =
            (steps == 0) ? 65536 : prv_throw_decay_q16(65536, steps);
    }
    s_throw_decay_table_ready = true;
}

static void prv_ensure_throw_decay_table(void)
{
    if(!s_throw_decay_table_ready) {
        prv_throw_decay_table_init();
    }
}

static int32_t prv_throw_decay(int32_t value, int32_t t)
{
    if(value == 0 || t <= 0) {
        return value;
    }
    if(t > LAUNCHER_SCROLL_DECAY_TABLE_MAX_MS) {
        return 0;
    }

    prv_ensure_throw_decay_table();
    return (int32_t)(((int64_t)value * s_throw_decay_table[t]) >> 16);
}

static void prv_vel_reset(launcher_scroll_t *scroll)
{
    memset(scroll->vel_hist, 0, sizeof(scroll->vel_hist));
    scroll->vel_hist_index = 0u;
    scroll->vel_last_ms = 0u;
    scroll->throw_vect = 0;
}

static void prv_vel_sample(launcher_scroll_t *scroll, int32_t delta,
                           int32_t dt_ms, uint32_t now_ms)
{
    if(dt_ms <= 0) {
        return;
    }
    if(dt_ms > LAUNCHER_SCROLL_VELOCITY_WINDOW_MS) {
        /* 采样间隔超出速度窗口，本次采样无法代表「松手瞬间」的速度。 */
        prv_vel_reset(scroll);
        scroll->vel_last_ms = now_ms;
        return;
    }

    scroll->vel_hist[scroll->vel_hist_index].delta = delta;
    scroll->vel_hist[scroll->vel_hist_index].dt_ms = dt_ms;
    scroll->vel_hist[scroll->vel_hist_index].tick = now_ms;
    scroll->vel_hist_index =
        (scroll->vel_hist_index + 1u) % (uint32_t)LAUNCHER_SCROLL_VELOCITY_WINDOW;
    scroll->vel_last_ms = now_ms;
}

/*
 * 估算松手瞬间的指针速度（px/100 ms 窗口）。
 *
 * 对窗口内每个采样 v_i = delta_i / dt_i 做时间加权平均：
 *
 *     w_i = exp(-k * age_i)，age 取采样区间中点
 *     v   = Σ (w_i * dt_i * v_i) / Σ (w_i * dt_i)
 *         = Σ (w_i * delta_i)    / Σ (w_i * dt_i)
 *
 * 分子量纲 px、分母量纲 ms，相除即 px/ms，再乘 100 得到与 LVGL
 * scroll_throw_vect 同量纲的甩动距离。
 *
 * 该形式没有初值：纯滑窗加权平均，因此同一物理手势在任意采样率下都得到同一个
 * 速度。LVGL 直接对位移做年龄加权求和，结果会随采样密度变化；带初值的 EMA 也
 * 会因初值权重随窗口长度变化而产生同样的偏差。
 */
static int32_t prv_vel_total(const launcher_scroll_t *scroll, uint32_t now_ms)
{
    int64_t numerator = 0;   /* Σ delta_i * w_i  (px) */
    int64_t denominator = 0; /* Σ dt_i    * w_i  (ms) */

    for(uint32_t index = 0u; index < (uint32_t)LAUNCHER_SCROLL_VELOCITY_WINDOW; index++) {
        const int32_t delta = scroll->vel_hist[index].delta;
        const int32_t dt_ms = scroll->vel_hist[index].dt_ms;
        const uint32_t tick = scroll->vel_hist[index].tick;

        if(delta == 0 || dt_ms <= 0 || tick == 0u) {
            continue;
        }

        /* delta 覆盖 [tick - dt, tick]，代表时刻是区间中点。 */
        const int32_t age = (int32_t)(now_ms - tick) + dt_ms / 2;
        const int32_t weight = prv_throw_decay(65536, age); /* Q16 */

        if(weight == 0) {
            continue;
        }

        numerator += (int64_t)delta * weight;
        denominator += (int64_t)dt_ms * weight;
    }

    if(denominator == 0) {
        return 0;
    }

    int64_t result = (numerator * LAUNCHER_SCROLL_THROW_STEP_MS) / denominator;

    if(result > LAUNCHER_SCROLL_MAX_THROW) {
        result = LAUNCHER_SCROLL_MAX_THROW;
    }
    if(result < -LAUNCHER_SCROLL_MAX_THROW) {
        result = -LAUNCHER_SCROLL_MAX_THROW;
    }
    return (int32_t)result;
}

/*
 * DIRECT_DRAG_ONLY 的指针处理：只做「pointer 位移 → logical 位移」的 1:1 映射。
 *
 * 刻意不调用 prv_vel_sample() / prv_vel_total() / prv_bound_delta()：
 * 阈值判定沿用与 native 相同的 10 px（否则 tap 会被误判为拖动），但越过阈值后
 * 每一个 pointer 采样都立即全额生效，没有阈值丢弃、没有弹性缩放、没有速度估计。
 */
static void prv_direct_drag_move(launcher_scroll_t *scroll, int32_t point_x)
{
    const int32_t delta = point_x - scroll->prev_point_x;

    scroll->prev_point_x = point_x;
    scroll->drag_scroll_sum += delta;

    if(scroll->state != LAUNCHER_SCROLL_DRAGGING) {
        if(scroll->drag_scroll_sum > LAUNCHER_SCROLL_DIRECTION_LIMIT ||
           scroll->drag_scroll_sum < -LAUNCHER_SCROLL_DIRECTION_LIMIT) {
            scroll->state = LAUNCHER_SCROLL_DRAGGING;
        }
        return;
    }

    if(delta == 0) {
        return;
    }

    /* 手指左移（delta < 0）→ logical_scroll_x 增大，方向与 native 一致。
     * 全额生效：不做弹性缩放、不丢弃阈值采样。计数由 prv_apply() 统一完成。 */
    prv_apply(scroll, scroll->position - delta);
}

static void prv_direct_drag_tick(launcher_scroll_t *scroll, bool pressed,
                                 int32_t point_x)
{
    if(pressed) {
        if(scroll->prev_pressed) {
            prv_direct_drag_move(scroll, point_x);
        }
        else {
            /* 按下：记录起点、清速度、取消一切 pending 运动。 */
            scroll->state = LAUNCHER_SCROLL_PRESSED;
            scroll->snap_active = false;
            scroll->drag_scroll_sum = 0;
            scroll->prev_point_x = point_x;
            scroll->press_was_scrolling = false;
            scroll->throw_vect = 0;
            prv_vel_reset(scroll);
        }
    }
    else {
        /* 松手：立即停止。不估算速度、不进入惯性、不吸附、不补间。 */
        if(scroll->prev_pressed) {
            scroll->press_was_scrolling = (scroll->state == LAUNCHER_SCROLL_DRAGGING);
            scroll->drag_scroll_sum = 0;
            scroll->throw_vect = 0;
            prv_vel_reset(scroll);
        }
        scroll->state = LAUNCHER_SCROLL_IDLE;
    }
}

void launcher_scroll_init(launcher_scroll_t *scroll,
                          const launcher_scroll_config_t *cfg,
                          launcher_scroll_apply_cb_t apply_cb,
                          void *user_data)
{
    if(scroll == NULL) {
        return;
    }

    memset(scroll, 0, sizeof(*scroll));

    if(cfg != NULL) {
        scroll->config = *cfg;
    }
    if(scroll->config.max_position < 0) {
        scroll->config.max_position = 0;
    }
    if(scroll->config.slot_pitch <= 0) {
        scroll->config.slot_pitch = 1;
    }
    if(scroll->config.slot_count < 0) {
        scroll->config.slot_count = 0;
    }

    scroll->apply_cb = apply_cb;
    scroll->user_data = user_data;
    scroll->position = 0;
    scroll->state = LAUNCHER_SCROLL_IDLE;
    scroll->snap_active = false;
    scroll->press_was_scrolling = false;
    scroll->initialized = true;
    prv_ensure_throw_decay_table();
    prv_vel_reset(scroll);
}

int32_t launcher_scroll_estimate_throw(const launcher_scroll_t *scroll, uint32_t now_ms)
{
    if(scroll == NULL || !scroll->initialized) {
        return 0;
    }
    return prv_vel_total(scroll, now_ms);
}

void launcher_scroll_set_position(launcher_scroll_t *scroll, int32_t position)
{
    if(scroll == NULL || !scroll->initialized) {
        return;
    }
    prv_apply(scroll, position);
}

void launcher_scroll_cancel(launcher_scroll_t *scroll)
{
    if(scroll == NULL || !scroll->initialized) {
        return;
    }

    scroll->state = LAUNCHER_SCROLL_IDLE;
    scroll->snap_active = false;
    scroll->drag_scroll_sum = 0;
    scroll->prev_pressed = false;
    prv_vel_reset(scroll);
}

int32_t launcher_scroll_nearest_index(const launcher_scroll_t *scroll, int32_t position)
{
    if(scroll == NULL || scroll->config.slot_count <= 0) {
        return 0;
    }

    const int32_t pitch = scroll->config.slot_pitch;
    int32_t index = (position - scroll->config.snap_anchor_x + pitch / 2) / pitch;

    if(index < 0) {
        index = 0;
    }
    if(index > scroll->config.slot_count - 1) {
        index = scroll->config.slot_count - 1;
    }
    return index;
}

/*
 * 吸附目标 = 最近的网格点，并且 max_position 本身永远是一个吸附点。
 *
 * 这一点必须有：content width 含左侧内边距，max_position 不一定落在
 * anchor + k*pitch 上（当前几何下 1860 = 8*220 + 100）。若 max 不是吸附点，
 * 一次甩到底会被夹紧在 max，随后吸附又把它拉回上一个网格点，形成可见回弹。
 */
int32_t launcher_scroll_nearest_snap(const launcher_scroll_t *scroll, int32_t position)
{
    if(scroll == NULL) {
        return 0;
    }

    const int32_t clamped = prv_clamp_position(scroll, position);
    const int32_t max_position = scroll->config.max_position;

    if(clamped >= max_position) {
        return max_position;
    }

    if(scroll->config.slot_count <= 0) {
        return clamped;
    }

    int32_t target = scroll->config.snap_anchor_x +
                     launcher_scroll_nearest_index(scroll, clamped) * scroll->config.slot_pitch;

    if(target < 0) {
        target = 0;
    }
    if(target > max_position) {
        target = max_position;
    }

    /* 端点与最近网格点一样近（或更近）时选端点，避免 max 附近来回摆动。 */
    if((max_position - clamped) <= (clamped - target)) {
        return max_position;
    }
    return target;
}

bool launcher_scroll_take_scroll_gesture(launcher_scroll_t *scroll)
{
    if(scroll == NULL) {
        return false;
    }

    const bool value = scroll->press_was_scrolling;
    scroll->press_was_scrolling = false;
    return value;
}

static void prv_snap_begin(launcher_scroll_t *scroll, int32_t target, uint32_t now_ms)
{
    if(target == scroll->position) {
        scroll->state = LAUNCHER_SCROLL_IDLE;
        scroll->snap_active = false;
        return;
    }

    scroll->snap_from = scroll->position;
    scroll->snap_to = target;
    scroll->snap_start_ms = now_ms;
    scroll->snap_active = true;
    scroll->state = LAUNCHER_SCROLL_SNAPPING;
}

static void prv_snap_step(launcher_scroll_t *scroll, uint32_t now_ms)
{
    const uint32_t elapsed = now_ms - scroll->snap_start_ms;

    if(elapsed >= (uint32_t)LAUNCHER_SCROLL_SNAP_MS) {
        prv_apply(scroll, scroll->snap_to);
        scroll->snap_active = false;
        scroll->state = LAUNCHER_SCROLL_IDLE;
        return;
    }

    /* ease-out：1 - (1 - t)^2，与 LVGL 的 lv_anim_path_ease_out 同形。 */
    const int32_t remaining = LAUNCHER_SCROLL_SNAP_MS - (int32_t)elapsed;
    const int32_t total = LAUNCHER_SCROLL_SNAP_MS;
    const int32_t span = scroll->snap_to - scroll->snap_from;
    const int32_t shaped = (span * (total * total - remaining * remaining)) / (total * total);

    prv_apply(scroll, scroll->snap_from + shaped);
}

static void prv_inertia_step(launcher_scroll_t *scroll, int32_t elapsed_ms)
{
    if(elapsed_ms <= 0) {
        return;
    }

    /*
     * throw_vect 与 LVGL 的 pointer.scroll_throw_vect 同为「每 100 ms 窗口的位移」
     * 量纲，而 indev_scroll_throw_decay() 的 t 是真实经过毫秒数。因此这里直接用
     * elapsed_ms 衰减，不能再乘衰减百分比——否则 t >= 99 会立刻把速度清零。
     *
     * 速度 px/ms = throw_vect / 100，故本帧位移 = throw_vect * elapsed / 100。
     */
    scroll->throw_vect = prv_throw_decay(scroll->throw_vect, elapsed_ms);

    int32_t step = (scroll->throw_vect * elapsed_ms) / LAUNCHER_SCROLL_THROW_STEP_MS;

    if(step != 0) {
        step = prv_bound_delta(scroll, step);
    }
    if(step != 0) {
        prv_apply(scroll, scroll->position + step);
    }

    const bool at_bound = (scroll->position == 0) ||
                          (scroll->position == scroll->config.max_position);

    /* throw_vect 是「每 100 ms 剩余位移」：低于停止阈值后，即使按当前帧率继续
     * 推进也不足 1 px，因此直接转入吸附。阈值是纯时间的，与帧率无关。 */
    if(scroll->throw_vect < LAUNCHER_SCROLL_STOP_THRESHOLD ||
       (step == 0 && at_bound)) {
        prv_vel_reset(scroll);

        const int32_t target = launcher_scroll_nearest_snap(scroll, scroll->position);
        if(target != scroll->position) {
            prv_snap_begin(scroll, target, scroll->last_tick);
        }
        else {
            scroll->state = LAUNCHER_SCROLL_IDLE;
        }
    }
}

static void prv_release(launcher_scroll_t *scroll, uint32_t now_ms)
{
    if(scroll->state == LAUNCHER_SCROLL_DRAGGING) {
        /* LVGL 在 release 时把已经衰减过的 throw vector 直接交给 throw 动画。 */
        scroll->throw_vect = prv_bound_delta(scroll, prv_vel_total(scroll, now_ms));
        scroll->state = LAUNCHER_SCROLL_INERTIA;
        scroll->press_was_scrolling = true;
    }
    else {
        /* 未越过阈值：与 native 一样什么都不发生（不位移、不吸附）。 */
        scroll->state = LAUNCHER_SCROLL_IDLE;
    }

    scroll->drag_scroll_sum = 0;
}

static void prv_move(launcher_scroll_t *scroll, int32_t point_x,
                     int32_t frame_dt_ms, uint32_t now_ms)
{
    const int32_t delta = point_x - scroll->prev_point_x;

    scroll->prev_point_x = point_x;

    /* 与 native 相同：先累计位移，越过阈值才认定为滚动；越过阈值的那一个采样
     * 被阈值检测本身吸收，strip 从下一个采样开始移动。 */
    scroll->drag_scroll_sum += delta;

    if(scroll->state != LAUNCHER_SCROLL_DRAGGING) {
        if(scroll->drag_scroll_sum > LAUNCHER_SCROLL_DIRECTION_LIMIT ||
           scroll->drag_scroll_sum < -LAUNCHER_SCROLL_DIRECTION_LIMIT) {
            scroll->state = LAUNCHER_SCROLL_DRAGGING;
            prv_vel_reset(scroll);
        }
        return;
    }

    if(delta == 0) {
        return;
    }

    /* 方向与 native 一致：手指左移（delta < 0）时 logical_scroll_x 增大。
     * 按位移采样而不是按帧采样，因此触摸比渲染快时自然合并到最新位置。 */
    const int32_t bounded = prv_bound_delta(scroll, -delta);
    if(bounded != 0) {
        prv_apply(scroll, scroll->position + bounded);
    }

    /* 速度样本记录未做弹性缩放的真实位移与真实采样间隔。 */
    prv_vel_sample(scroll, -delta, frame_dt_ms, now_ms);
}

bool launcher_scroll_tick(launcher_scroll_t *scroll, bool pressed,
                          int32_t point_x, uint32_t now_ms)
{
    if(scroll == NULL || !scroll->initialized) {
        return false;
    }

    const int32_t before = scroll->position;
    /* 先记录本帧时间戳，prv_move() 才能用「上一帧 → 本帧」的真实间隔做速度归一化。 */
    const int32_t frame_dt_ms = (int32_t)(now_ms - scroll->last_tick);
    scroll->last_tick = now_ms;

    ++s_motion_samples;

    if(prv_direct_drag_active()) {
        /*
         * MODE 1：整条自动运动链路（速度估算 → throw → 惯性 → 吸附 → 补间）
         * 在下面这个分支里被整体绕过，不执行任何一行。
         */
        prv_direct_drag_tick(scroll, pressed, point_x);
        scroll->prev_pressed = pressed;
        scroll->drag_last_ms = now_ms;
        return scroll->position != before;
    }

    if(pressed) {
        if(scroll->state == LAUNCHER_SCROLL_DRAGGING) {
            prv_move(scroll, point_x, frame_dt_ms, now_ms);
        }
        else if(!scroll->prev_pressed) {
            /* 新按下：native 会在这一刻取消仍在进行的 throw 动画。 */
            scroll->state = LAUNCHER_SCROLL_PRESSED;
            scroll->snap_active = false;
            scroll->drag_scroll_sum = 0;
            scroll->prev_point_x = point_x;
            scroll->press_was_scrolling = false;
            prv_vel_reset(scroll);
        }
        else if(scroll->state == LAUNCHER_SCROLL_PRESSED) {
            prv_move(scroll, point_x, frame_dt_ms, now_ms);
        }
        else {
            /* 吸附动画期间按下：不接管，等调用方在需要时 cancel()。 */
        }
    }
    else {
        if(scroll->prev_pressed) {
            prv_release(scroll, now_ms);
        }

        if(scroll->state == LAUNCHER_SCROLL_INERTIA) {
            prv_inertia_step(scroll, (int32_t)(now_ms - scroll->drag_last_ms));
        }
        else if(scroll->state == LAUNCHER_SCROLL_SNAPPING) {
            prv_snap_step(scroll, now_ms);
        }
    }

    scroll->prev_pressed = pressed;
    scroll->drag_last_ms = now_ms;

    return scroll->position != before;
}
