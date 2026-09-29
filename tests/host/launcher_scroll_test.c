/*
 * launcher_scroll_test.c
 *
 * Launcher 逻辑滚动控制器的确定性 host 测试。
 *
 * 目标板上的真实手势无法在 host 复现，但控制器本身是纯逻辑：给定时间戳与
 * 指针采样序列，它的位置/边界/惯性/吸附必须完全确定。本测试因此逐条钉死
 * 迁移后必须保持的交互语义：
 *
 *   1. 拖动 1:1，方向与 native 一致；
 *   2. 10 px 阈值行为与 lv_indev_find_scroll_obj 一致；
 *   3. 边界夹紧与弹性只在越界时生效；
 *   4. 吸附网格对齐 slot 基准位置；
 *   5. 惯性基于真实 dt（同样轨迹、不同帧率下不应发散）；
 *   6. 位置永不越界、永不漂移；
 *   7. 滚动过的手势才会抑制 click；
 *   8. cancel() 后不再有任何位置更新（scene teardown 安全）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "launcher_scroll.h"

/* 与 ui_screen_launcher.c 相同的几何。 */
#define SLOT_COUNT   12
#define SLOT_WIDTH   200
#define SLOT_SPACING 20
#define SLOT_PITCH   (SLOT_WIDTH + SLOT_SPACING)
#define SLOT_FIRST_X 20
#define VIEWPORT_W   800
#define CONTENT_W    (SLOT_COUNT * SLOT_PITCH + 20)
#define MAX_SCROLL   (CONTENT_W - VIEWPORT_W)

/* 吸附网格原点：卡带内容按 pitch 排布，网格必须与卡带 pitch 对齐（而不是与
 * 首个 slot 的 x 重叠），否则「一次甩到底」的 max_position 会落在网格之外。 */
#define SNAP_ANCHOR  0

static int s_failures;
static const char *s_case = "";

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        if(!(cond)) {                                                             \
            (void)printf("FAIL [%s] %s:%d: ", s_case, __FILE__, __LINE__);        \
            (void)printf(__VA_ARGS__);                                            \
            (void)printf("\n");                                                   \
            ++s_failures;                                                         \
        }                                                                         \
    } while(0)

static launcher_scroll_t make_scroll(launcher_scroll_apply_cb_t cb, void *user)
{
    launcher_scroll_t scroll;
    const launcher_scroll_config_t cfg = {
        .position = 0,
        .max_position = MAX_SCROLL,
        .snap_anchor_x = SNAP_ANCHOR,
        .slot_pitch = SLOT_PITCH,
        .slot_count = SLOT_COUNT,
    };

    launcher_scroll_init(&scroll, &cfg, cb, user);
    return scroll;
}

/* ------------------------------------------------------------------ */
/*  1. 几何常量自检                                                     */
/* ------------------------------------------------------------------ */

static void test_geometry_constants(void)
{
    s_case = "geometry";

    CHECK(MAX_SCROLL == 1860, "max scroll expected 1860, got %d", MAX_SCROLL);
    CHECK(SLOT_FIRST_X + SLOT_WIDTH <= VIEWPORT_W,
          "first slot must fit fully in the viewport at position 0");
    /* 最后一个 slot 必须能通过滚动被完整看到：max scroll 之后它的右边缘
     * 不能超过视口，否则尾部内容永远无法到达。 */
    CHECK(SLOT_FIRST_X + (SLOT_COUNT - 1) * SLOT_PITCH >= MAX_SCROLL,
          "last slot base %d must reach at least max scroll %d",
          SLOT_FIRST_X + (SLOT_COUNT - 1) * SLOT_PITCH, MAX_SCROLL);
}

/* ------------------------------------------------------------------ */
/*  2. 约束验证（不变量）                                               */
/* ------------------------------------------------------------------ */

static void test_drag_is_one_to_one(void)
{
    s_case = "drag-1to1";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 1000u;

    /* 按下 */
    (void)launcher_scroll_tick(&scroll, true, 400, now);

    /* 首帧 5 px 不足以越过阈值：位置不动。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 395, now);
    CHECK(scroll.position == 0, "sub-threshold move must not scroll, got %d", scroll.position);

    /* 累计 12 px 越过阈值：该采样被阈值吸收，位置仍不动。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 388, now);
    CHECK(scroll.position == 0, "threshold-crossing sample must be absorbed, got %d",
          scroll.position);

    /* 之后 1:1。手指左移 30 px → logical +30。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 358, now);
    CHECK(scroll.position == 30, "expected 1:1 +30, got %d", scroll.position);

    /* 反向 1:1。手指右移 12 px → logical -12。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 370, now);
    CHECK(scroll.position == 18, "expected 1:1 reverse -12 -> 18, got %d", scroll.position);
}

static void test_bounds(void)
{
    s_case = "bounds";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 2000u;

    /* 拖到远超右边界。 */
    (void)launcher_scroll_tick(&scroll, true, 400, now);
    for(int i = 0; i < 400; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, true, 400 - (i + 1) * 20, now);
    }
    CHECK(scroll.position == MAX_SCROLL, "must clamp at max, got %d", scroll.position);

    (void)launcher_scroll_tick(&scroll, false, 0, now + 10u);

    /* 反向拖到远超左边界。 */
    now += 20u;
    (void)launcher_scroll_tick(&scroll, true, 0, now);
    for(int i = 0; i < 400; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, true, (i + 1) * 20, now);
    }
    CHECK(scroll.position == 0, "must clamp at 0, got %d", scroll.position);
}

static void test_set_position_clamps(void)
{
    s_case = "set-position";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);

    launcher_scroll_set_position(&scroll, 99999);
    CHECK(scroll.position == MAX_SCROLL, "set_position must clamp high, got %d", scroll.position);

    launcher_scroll_set_position(&scroll, -500);
    CHECK(scroll.position == 0, "set_position must clamp low, got %d", scroll.position);

    launcher_scroll_set_position(&scroll, 220);
    CHECK(scroll.position == 220, "set_position must accept in-range, got %d", scroll.position);
}

/* ------------------------------------------------------------------ */
/*  3. 吸附网格                                                         */
/* ------------------------------------------------------------------ */

static void test_snap_grid(void)
{
    s_case = "snap-grid";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);

    static const struct {
        int32_t position;
        int32_t expected_snap;
    } cases[] = {
        {0, 0},
        {10, 0},
        {109, 0},
        {111, 220},
        {240, 220},
        {329, 220},
        {330, 440},   /* 正好在中点：整数除法向 0 截断，与 LVGL 取整方向一致 */
        {331, 440},
        {1760, 1760},
        {1780, 1860},   /* 1780 距端点 80、距 1760 仅 20 → 1760；见下方断言 */
        {1860, 1860},   /* max 本身必须是一个吸附点 */
        {1859, 1860},
    };

    /* 前 9 条与最后三条分开校验，避免把「端点优先」规则写死在中间格上。 */
    for(size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if(cases[i].position == 1780) {
            continue;
        }
        const int32_t snap = launcher_scroll_nearest_snap(&scroll, cases[i].position);
        CHECK(snap == cases[i].expected_snap,
              "nearest_snap(%d) expected %d got %d",
              cases[i].position, cases[i].expected_snap, snap);
    }

    CHECK(launcher_scroll_nearest_snap(&scroll, 1780) == 1760,
          "1780 is nearer to 1760 than to the endpoint");

    /* 网格点必须严格递增且都在 [0, max] 内，最后一个网格点为 1760。 */
    int32_t previous = -1;
    for(int32_t index = 0; index < SLOT_COUNT; index++) {
        const int32_t grid = SNAP_ANCHOR + index * SLOT_PITCH;
        if(grid > MAX_SCROLL) {
            break;
        }
        CHECK(grid > previous, "grid must strictly increase at index %d", index);
        CHECK(grid >= 0 && grid <= MAX_SCROLL, "grid %d out of range", grid);
        CHECK(launcher_scroll_nearest_snap(&scroll, grid) == grid,
              "grid point %d must be its own snap target", grid);
        previous = grid;
    }
    CHECK(previous == 1760, "last grid point expected 1760, got %d", previous);

    CHECK(launcher_scroll_nearest_snap(&scroll, MAX_SCROLL) == MAX_SCROLL,
          "max scroll must itself be a snap point");
    CHECK(launcher_scroll_nearest_snap(&scroll, 99999) == MAX_SCROLL,
          "out-of-range must clamp to max snap");
    CHECK(launcher_scroll_nearest_snap(&scroll, -99999) == 0,
          "negative must clamp to 0");
}

static void test_snap_after_release(void)
{
    s_case = "snap-release";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 5000u;

    /* 慢速拖到 100 后松手：无惯性，应吸附到 130（100 距 130 更近）。 */
    (void)launcher_scroll_tick(&scroll, true, 400, now);
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 388, now);
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 288, now);

    CHECK(scroll.position == 100, "expected drag to 100, got %d", scroll.position);

    /* 松手后慢速采样：速度很低，最终必须停在吸附点。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, false, 0, now);
    for(int i = 0; i < 200; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, false, 0, now);
        if(scroll.state == LAUNCHER_SCROLL_IDLE) {
            break;
        }
    }

    CHECK(scroll.state == LAUNCHER_SCROLL_IDLE, "must settle to IDLE, got %d", scroll.state);
    CHECK(launcher_scroll_nearest_snap(&scroll, scroll.position) == scroll.position,
          "settled position %d must be a snap point", scroll.position);
    CHECK(scroll.position >= 0 && scroll.position <= MAX_SCROLL,
          "settled position %d out of bounds", scroll.position);
}

/* ------------------------------------------------------------------ */
/*  4. 惯性：真实 dt                                                    */
/* ------------------------------------------------------------------ */

/* 快速甩动后：惯性阶段位置必须单调前进，随后吸附到网格点且不越界。 */
static void test_fling_settles_on_grid(void)
{
    s_case = "fling";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 8000u;

    (void)launcher_scroll_tick(&scroll, true, 700, now);
    /* 快速左甩：-60 px/10ms。 */
    for(int i = 0; i < 6; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, true, 700 - (i + 1) * 60, now);
    }

    const int32_t at_release = scroll.position;
    CHECK(at_release > 0, "fast drag must scroll, got %d", scroll.position);

    now += 10u;
    (void)launcher_scroll_tick(&scroll, false, 0, now);
    CHECK(scroll.throw_vect > 0, "release must produce a throw, got %d", scroll.throw_vect);

    /* 惯性阶段（state == INERTIA）必须单调前进；吸附阶段允许回退到网格点。 */
    int32_t previous = scroll.position;
    bool inertia_monotonic = true;
    int guard = 0;

    while(scroll.state != LAUNCHER_SCROLL_IDLE && guard++ < 1000) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, false, 0, now);

        if(scroll.state == LAUNCHER_SCROLL_INERTIA && scroll.position < previous) {
            inertia_monotonic = false;
        }
        previous = scroll.position;
        CHECK(scroll.position >= 0 && scroll.position <= MAX_SCROLL,
              "inertia went out of bounds: %d", scroll.position);
    }

    CHECK(guard < 1000, "fling must terminate");
    CHECK(inertia_monotonic, "left fling must not move backwards during inertia");
    CHECK(previous > at_release, "fling must advance beyond release point");
    CHECK(launcher_scroll_nearest_snap(&scroll, scroll.position) == scroll.position,
          "fling must settle on a snap point, got %d", scroll.position);
    CHECK(scroll.position <= MAX_SCROLL, "settled beyond max: %d", scroll.position);
}

/*
 * 同一物理手势（恒定 v px/ms）、不同采样周期：松手瞬间估算出的甩动速度必须
 * 接近。直接查询估算器，避免被 release tick 已经消费掉的惯性干扰。
 *
 * 采样位移要随周期等比放大，才代表同一个手势。
 */
static void test_fling_velocity_is_frame_rate_independent(void)
{
    s_case = "fling-dt";
    const uint32_t periods[3] = {10u, 20u, 40u};
    const int32_t v_px_per_ms = 4;
    int32_t estimates[3] = {0, 0, 0};

    for(int run = 0; run < 3; run++) {
        launcher_scroll_t scroll = make_scroll(NULL, NULL);
        const uint32_t period = periods[run];
        const int32_t step_px = v_px_per_ms * (int32_t)period;
        uint32_t now = 10000u;
        int32_t x = 760;

        (void)launcher_scroll_tick(&scroll, true, x, now);
        for(int i = 0; i < 6; i++) {
            now += period;
            x -= step_px;
            (void)launcher_scroll_tick(&scroll, true, x, now);
        }

        /* 期望 4 px/ms = 400 px/100ms，不触发 500 px 上限。 */
        estimates[run] = launcher_scroll_estimate_throw(&scroll, now);

        int guard = 0;
        while(scroll.state != LAUNCHER_SCROLL_IDLE && guard++ < 4000) {
            now += period;
            (void)launcher_scroll_tick(&scroll, false, 0, now);
        }
        CHECK(scroll.position >= 0 && scroll.position <= MAX_SCROLL,
              "period %u settled out of bounds: %d", period, scroll.position);
        CHECK(launcher_scroll_nearest_snap(&scroll, scroll.position) == scroll.position,
              "period %u must settle on a snap point, got %d", period, scroll.position);
    }

    for(int run = 0; run < 3; run++) {
        CHECK(estimates[run] > 0, "period %u must produce a velocity, got %d",
              periods[run], estimates[run]);
    }

    for(int run = 1; run < 3; run++) {
        const int32_t diff = estimates[0] > estimates[run] ? estimates[0] - estimates[run]
                                                           : estimates[run] - estimates[0];
        const int32_t reference = estimates[0] > estimates[run] ? estimates[0] : estimates[run];

        CHECK(diff * 100 <= reference * 15,
              "velocity estimate must be dt based: period %u gave %d vs period %u gave %d",
              periods[0], estimates[0], periods[run], estimates[run]);
    }
}

/* ------------------------------------------------------------------ */
/*  5. click 抑制语义                                                   */
/* ------------------------------------------------------------------ */

static void test_scroll_gesture_flag(void)
{
    s_case = "click-suppression";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 20000u;

    /* 纯点击（无位移）：不算滚动手势。 */
    (void)launcher_scroll_tick(&scroll, true, 400, now);
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 402, now);
    now += 10u;
    (void)launcher_scroll_tick(&scroll, false, 402, now);
    CHECK(!launcher_scroll_take_scroll_gesture(&scroll),
          "tap must not be reported as a scroll gesture");

    /* 拖动：算滚动手势，且 take 之后被清除。 */
    now += 10u;
    (void)launcher_scroll_tick(&scroll, true, 400, now);
    for(int i = 0; i < 4; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, true, 400 - (i + 1) * 30, now);
    }
    now += 10u;
    (void)launcher_scroll_tick(&scroll, false, 0, now);

    CHECK(launcher_scroll_take_scroll_gesture(&scroll),
          "drag must be reported as a scroll gesture");
    CHECK(!launcher_scroll_take_scroll_gesture(&scroll),
          "scroll gesture flag must clear after take");
}

/* ------------------------------------------------------------------ */
/*  6. cancel / scene teardown 安全                                     */
/* ------------------------------------------------------------------ */

static int s_apply_calls;
static void counting_apply(int32_t position, void *user)
{
    (void)position;
    (void)user;
    ++s_apply_calls;
}

static void test_cancel_stops_updates(void)
{
    s_case = "cancel";
    launcher_scroll_t scroll = make_scroll(counting_apply, NULL);
    uint32_t now = 30000u;

    (void)launcher_scroll_tick(&scroll, true, 700, now);
    for(int i = 0; i < 6; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, true, 700 - (i + 1) * 60, now);
    }
    now += 10u;
    (void)launcher_scroll_tick(&scroll, false, 0, now);
    CHECK(scroll.state == LAUNCHER_SCROLL_INERTIA, "expected inertia, got %d", scroll.state);

    launcher_scroll_cancel(&scroll);
    CHECK(scroll.state == LAUNCHER_SCROLL_IDLE, "cancel must return to IDLE");
    CHECK(scroll.throw_vect == 0, "cancel must clear throw vector");

    s_apply_calls = 0;
    for(int i = 0; i < 100; i++) {
        now += 10u;
        (void)launcher_scroll_tick(&scroll, false, 0, now);
    }
    CHECK(s_apply_calls == 0, "no position updates allowed after cancel, got %d",
          s_apply_calls);
}

/* ------------------------------------------------------------------ */
/*  7. 长跑无漂移                                                       */
/* ------------------------------------------------------------------ */

static void test_no_long_run_drift(void)
{
    s_case = "drift";
    launcher_scroll_t scroll = make_scroll(NULL, NULL);
    uint32_t now = 40000u;
    int32_t point = 400;

    /* 1200 个采样、来回拖动，最后必须精确回到起点。 */
    (void)launcher_scroll_tick(&scroll, true, point, now);

    for(int i = 0; i < 1200; i++) {
        now += 10u;
        point += ((i / 40) % 2 == 0) ? -7 : 7;
        if(point < 60) {
            point = 60;
        }
        if(point > 740) {
            point = 740;
        }
        (void)launcher_scroll_tick(&scroll, true, point, now);
    }

    /* 反向走完全相同的位移序列，位置必须回到 0。 */
    for(int i = 1199; i >= 0; i--) {
        now += 10u;
        point -= ((i / 40) % 2 == 0) ? -7 : 7;
        (void)launcher_scroll_tick(&scroll, true, point, now);
    }

    CHECK(scroll.position == 0,
          "symmetric drag must return to 0, got %d (cumulative coordinate drift)", scroll.position);

    /* 位置必须始终在界内。 */
    CHECK(scroll.position >= 0 && scroll.position <= MAX_SCROLL,
          "position out of bounds: %d", scroll.position);
}

int main(void)
{
    test_geometry_constants();
    test_drag_is_one_to_one();
    test_bounds();
    test_set_position_clamps();
    test_snap_grid();
    test_snap_after_release();
    test_fling_settles_on_grid();
    test_fling_velocity_is_frame_rate_independent();
    test_scroll_gesture_flag();
    test_cancel_stops_updates();
    test_no_long_run_drift();

    if(s_failures != 0) {
        (void)printf("\nlauncher_scroll_test: %d check(s) FAILED\n", s_failures);
        return 1;
    }

    (void)printf("launcher_scroll_test: all checks passed\n");
    return 0;
}
