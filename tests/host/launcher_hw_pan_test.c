/*
 * Host regression test for the Launcher LTDC hardware-pan core.
 *
 * 覆盖范围（全部为 production 纯逻辑，不依赖 HAL / LVGL）：
 *   - scroll source address 计算与 0..1860 边界
 *   - stride / viewport / geometry 自洽校验
 *   - pan 提交与 coalescing（latest-position-wins）
 *   - reload owner 路由（pan 完成绝不能确认 LVGL flush）
 *   - 显示模式状态机的转换与拒绝条件
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "display_mode.h"
#include "launcher_hw_pan.h"
#include "ltdc_reload.h"

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                       \
    do {                                                                     \
        const uint32_t a_ = (uint32_t)(actual);                              \
        const uint32_t e_ = (uint32_t)(expected);                            \
        ++g_checks;                                                           \
        if (a_ != e_) {                                                       \
            ++g_failures;                                                     \
            printf("FAIL %s:%d: %s = %lu, expected %lu\n",                   \
                   __FILE__, __LINE__, #actual,                              \
                   (unsigned long)a_, (unsigned long)e_);                    \
        }                                                                    \
    } while (0)

/* 与 sdram_layout.h 一致的 production 几何 */
#define STRIP_BASE    ((uintptr_t)0xD0465000UL)
#define STRIP_WIDTH   2660u
#define STRIP_HEIGHT  350u
#define STRIP_STRIDE  10656u
#define VIEWPORT_W    800u
#define SCROLL_MAX    (STRIP_WIDTH - VIEWPORT_W)

static const launcher_hw_pan_geometry_t k_geo = {
    .base_addr = STRIP_BASE,
    .stride_bytes = STRIP_STRIDE,
    .width = STRIP_WIDTH,
    .viewport_width = VIEWPORT_W,
    .scroll_max = SCROLL_MAX,
};

/* ------------------------------------------------------------ fake ops */

typedef struct {
    uintptr_t last_addr;
    uint32_t write_count;
    uint32_t vbr_count;
    bool accept_vbr;
} fake_hw_t;

static fake_hw_t g_hw;

static void fake_write_addr(uintptr_t addr, void *user)
{
    (void)user;
    g_hw.last_addr = addr;
    ++g_hw.write_count;
}

static bool fake_request_vbr(void *user)
{
    (void)user;
    ++g_hw.vbr_count;
    return g_hw.accept_vbr;
}

static const launcher_hw_pan_ops_t k_ops = {
    .write_source_addr = fake_write_addr,
    .request_vbr = fake_request_vbr,
    .user = NULL,
};

/*
 * 模拟一次真实的 LTDC ReloadEvent：
 * ISR 先从 owner 登记表取回完成事件，再分派给 pan 状态机。只调用其中一半会
 * 让 owner 表一直处于 pending，之后的提交全部被拒 —— 这正是生产 ISR 的顺序。
 */
static void complete_reload_event(launcher_hw_pan_t *pan)
{
    const uint32_t generation = ltdc_reload_pending_generation();
    const ltdc_reload_owner_t owner = ltdc_reload_complete_from_irq();
    if (owner == LTDC_RELOAD_OWNER_LAUNCHER_PAN) {
        launcher_hw_pan_on_reload_complete(pan, generation);
    }
}

static void reset_world(void)
{
    ltdc_reload_init();
    g_hw.last_addr = 0u;
    g_hw.write_count = 0u;
    g_hw.vbr_count = 0u;
    g_hw.accept_vbr = true;
}

/* ------------------------------------------------------------ geometry */

static void test_geometry(void)
{
    CHECK(launcher_hw_pan_geometry_is_valid(&k_geo));

    /* stride 必须覆盖逻辑行；10640 的原始行字节不应通过 32B 对齐校验 */
    launcher_hw_pan_geometry_t bad = k_geo;
    bad.stride_bytes = 10640u;
    CHECK(!launcher_hw_pan_geometry_is_valid(&bad));

    bad = k_geo;
    bad.stride_bytes = 10688u;   /* 也是 32 的倍数但大于预留 */
    CHECK(launcher_hw_pan_geometry_is_valid(&bad));

    bad = k_geo;
    bad.scroll_max = 1864u;      /* 用 padding 算出来的错误上界 */
    CHECK(!launcher_hw_pan_geometry_is_valid(&bad));

    bad = k_geo;
    bad.viewport_width = STRIP_WIDTH + 1u;
    CHECK(!launcher_hw_pan_geometry_is_valid(&bad));

    CHECK_EQ_U32(SCROLL_MAX, 1860u);
    CHECK_EQ_U32(launcher_hw_pan_clamp_x(&k_geo, -5), 0u);
    CHECK_EQ_U32(launcher_hw_pan_clamp_x(&k_geo, 0), 0u);
    CHECK_EQ_U32(launcher_hw_pan_clamp_x(&k_geo, 1860), 1860u);
    CHECK_EQ_U32(launcher_hw_pan_clamp_x(&k_geo, 1861), 1860u);
    CHECK_EQ_U32(launcher_hw_pan_clamp_x(&k_geo, 99999), 1860u);
}

static void test_source_address(void)
{
    CHECK(launcher_hw_pan_source_addr(&k_geo, 0) == STRIP_BASE);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 1) == STRIP_BASE + 4u);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 20) == STRIP_BASE + 80u);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 1860) == STRIP_BASE + 7440u);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 1860) ==
          (uintptr_t)0xD0466D10UL);

    /* 越界必须拒绝，而不是算出一个越界地址 */
    CHECK(launcher_hw_pan_source_addr(&k_geo, -1) == (uintptr_t)0);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 1861) == (uintptr_t)0);
    CHECK(launcher_hw_pan_source_addr(&k_geo, 2664) == (uintptr_t)0);

    /* 最大平移时最右可见像素仍在逻辑宽度内 */
    const uint32_t max_visible_x = SCROLL_MAX + VIEWPORT_W - 1u;
    CHECK_EQ_U32(max_visible_x, 2659u);
    CHECK(max_visible_x < STRIP_WIDTH);
}

/* ------------------------------------------------------------ pan 状态机 */

static void test_pan_submit_and_complete(void)
{
    reset_world();

    launcher_hw_pan_t pan;
    launcher_hw_pan_init(&pan, &k_geo, &k_ops, 0);
    CHECK(launcher_hw_pan_is_settled(&pan));

    /* 未使能时不提交 */
    CHECK(!launcher_hw_pan_set_x(&pan, 100));
    CHECK_EQ_U32(g_hw.vbr_count, 0u);

    launcher_hw_pan_set_enabled(&pan, true);

    /* 位置未变化不提交 */
    CHECK(!launcher_hw_pan_set_x(&pan, 0));
    CHECK_EQ_U32(g_hw.vbr_count, 0u);

    CHECK(launcher_hw_pan_set_x(&pan, 20));
    CHECK_EQ_U32(g_hw.vbr_count, 1u);
    CHECK_EQ_U32(g_hw.write_count, 1u);
    CHECK(g_hw.last_addr == STRIP_BASE + 80u);
    CHECK(pan.pending);
    CHECK(!launcher_hw_pan_is_settled(&pan));

    /* pending 期间重复 set_x 只合并，不再发 VBR（latest-position-wins） */
    CHECK(!launcher_hw_pan_set_x(&pan, 40));
    CHECK(!launcher_hw_pan_set_x(&pan, 137));
    CHECK_EQ_U32(g_hw.vbr_count, 1u);
    CHECK_EQ_U32(pan.coalesced, 2u);
    CHECK_EQ_U32(pan.desired_x, 137u);

    /* tick 在 pending 期间不得叠加请求 */
    CHECK(!launcher_hw_pan_tick(&pan));
    CHECK_EQ_U32(g_hw.vbr_count, 1u);

    /* 完成事件确认的是 requested_x（=20），不是 desired_x（=137） */
    complete_reload_event(&pan);
    CHECK_EQ_U32(pan.latched_x, 20u);
    CHECK(!pan.pending);
    CHECK(!launcher_hw_pan_is_settled(&pan));

    /* 下一次 tick 提交最新位置 */
    CHECK(launcher_hw_pan_tick(&pan));
    CHECK_EQ_U32(g_hw.vbr_count, 2u);
    CHECK(g_hw.last_addr == STRIP_BASE + 137u * 4u);

    complete_reload_event(&pan);
    CHECK_EQ_U32(pan.latched_x, 137u);
    CHECK(launcher_hw_pan_is_settled(&pan));
    CHECK_EQ_U32(pan.submits, 2u);
    CHECK_EQ_U32(pan.completions, 2u);
    CHECK_EQ_U32(pan.generation_mismatch, 0u);
    CHECK_EQ_U32(pan.vbr_rejected, 0u);
}

static void test_pan_bounds_and_latest_wins(void)
{
    reset_world();

    launcher_hw_pan_t pan;
    launcher_hw_pan_init(&pan, &k_geo, &k_ops, 0);
    launcher_hw_pan_set_enabled(&pan, true);

    /* 越界输入被夹住 */
    CHECK(launcher_hw_pan_set_x(&pan, 999999));
    CHECK_EQ_U32(pan.requested_x, 1860u);
    CHECK(g_hw.last_addr == STRIP_BASE + 1860u * 4u);
    complete_reload_event(&pan);
    CHECK_EQ_U32(pan.latched_x, 1860u);

    CHECK(launcher_hw_pan_set_x(&pan, -100));
    CHECK_EQ_U32(pan.requested_x, 0u);
    complete_reload_event(&pan);
    CHECK_EQ_U32(pan.latched_x, 0u);

    /* 快速拖拽：N 次输入采样只应产生远少于 N 次 VBR */
    reset_world();
    launcher_hw_pan_t fast;
    launcher_hw_pan_init(&fast, &k_geo, &k_ops, 0);
    launcher_hw_pan_set_enabled(&fast, true);

    const uint32_t samples = 50u;
    uint32_t completions = 0u;
    for (uint32_t i = 0u; i < samples; ++i) {
        (void)launcher_hw_pan_set_x(&fast, (int32_t)i * 10);
        if (fast.pending) {
            /* 模拟每 5 个采样才有一次 VBlank 完成 */
            if ((i % 5u) == 4u) {
                complete_reload_event(&fast);
                ++completions;
            }
        }
    }
    while (fast.pending) {
        complete_reload_event(&fast);
        ++completions;
    }
    (void)launcher_hw_pan_tick(&fast);
    if (fast.pending) {
        complete_reload_event(&fast);
        ++completions;
    }

    CHECK(fast.submits < samples);
    CHECK_EQ_U32(fast.coalesced, samples - fast.submits);
    /* 每个 VBlank 周期只确认一个位置，绝不排队显示每一个采样 */
    CHECK(completions <= fast.submits + 1u);
    CHECK(completions < samples);
    CHECK(fast.latched_x == fast.desired_x ||
          fast.desired_x == launcher_hw_pan_clamp_x(&k_geo, (int32_t)(samples - 1u) * 10));

    /* 最终位置必须收敛到最后一个采样 */
    (void)launcher_hw_pan_tick(&fast);
    if (fast.pending) {
        complete_reload_event(&fast);
    }
    CHECK(launcher_hw_pan_is_settled(&fast));
    CHECK_EQ_U32(fast.latched_x, launcher_hw_pan_clamp_x(&k_geo, (int32_t)(samples - 1u) * 10));
}

static void test_pan_disable_and_reject(void)
{
    reset_world();

    launcher_hw_pan_t pan;
    launcher_hw_pan_init(&pan, &k_geo, &k_ops, 0);
    launcher_hw_pan_set_enabled(&pan, true);

    CHECK(launcher_hw_pan_set_x(&pan, 100));
    CHECK(pan.pending);

    /* 停用必须放弃 pending，且不再占用 VBR owner */
    launcher_hw_pan_set_enabled(&pan, false);
    CHECK(!pan.pending);
    CHECK(!ltdc_reload_is_pending());
    CHECK(pan.desired_x == 100);

    /* VBR 被硬件拒绝时不留下半个 generation */
    reset_world();
    g_hw.accept_vbr = false;
    launcher_hw_pan_t rej;
    launcher_hw_pan_init(&rej, &k_geo, &k_ops, 0);
    launcher_hw_pan_set_enabled(&rej, true);
    CHECK(!launcher_hw_pan_set_x(&rej, 50));
    CHECK(!rej.pending);
    CHECK(ltdc_reload_is_pending() == false);
    CHECK_EQ_U32(rej.vbr_rejected, 1u);
}

static void test_generation_mismatch(void)
{
    reset_world();

    launcher_hw_pan_t pan;
    launcher_hw_pan_init(&pan, &k_geo, &k_ops, 0);
    launcher_hw_pan_set_enabled(&pan, true);

    CHECK(launcher_hw_pan_set_x(&pan, 42));
    const uint32_t gen = pan.pending_generation;

    /* 错误的 generation 不得被当成完成 */
    launcher_hw_pan_on_reload_complete(&pan, gen + 99u);
    CHECK(pan.pending);
    CHECK_EQ_U32(pan.generation_mismatch, 1u);
    CHECK_EQ_U32(pan.completions, 0u);

    launcher_hw_pan_on_reload_complete(&pan, gen);
    CHECK(!pan.pending);
    CHECK_EQ_U32(pan.completions, 1u);
}

/* --------------------------------------------------------- reload owner */

static void test_reload_owner_routing(void)
{
    reset_world();

    /* Launcher pan 的完成必须归还给 LAUNCHER_PAN，而不是 LVGL_FLUSH */
    const uint32_t pan_gen = ltdc_reload_arm(LTDC_RELOAD_OWNER_LAUNCHER_PAN);
    CHECK(pan_gen != 0u);
    CHECK(ltdc_reload_is_pending());
    CHECK(ltdc_reload_pending_owner() == LTDC_RELOAD_OWNER_LAUNCHER_PAN);

    const uint32_t observed_gen = ltdc_reload_pending_generation();
    const ltdc_reload_owner_t owner = ltdc_reload_complete_from_irq();
    CHECK(owner == LTDC_RELOAD_OWNER_LAUNCHER_PAN);
    CHECK(owner != LTDC_RELOAD_OWNER_LVGL_FLUSH);
    CHECK_EQ_U32(observed_gen, pan_gen);
    CHECK(!ltdc_reload_is_pending());

    /* LVGL flush 的完成归还给 LVGL_FLUSH */
    const uint32_t lvgl_gen = ltdc_reload_arm(LTDC_RELOAD_OWNER_LVGL_FLUSH);
    CHECK(lvgl_gen != 0u);
    CHECK(ltdc_reload_complete_from_irq() == LTDC_RELOAD_OWNER_LVGL_FLUSH);

    /* 每个 owner 的计数互不串线 */
    const ltdc_reload_state_t *st = ltdc_reload_state();
    CHECK_EQ_U32(st->owner_completions[LTDC_RELOAD_OWNER_LAUNCHER_PAN], 1u);
    CHECK_EQ_U32(st->owner_completions[LTDC_RELOAD_OWNER_LVGL_FLUSH], 1u);
    CHECK_EQ_U32(st->owner_arms[LTDC_RELOAD_OWNER_LAUNCHER_PAN], 1u);
    CHECK_EQ_U32(st->owner_arms[LTDC_RELOAD_OWNER_LVGL_FLUSH], 1u);
    CHECK_EQ_U32(st->orphan_completions, 0u);
    CHECK_EQ_U32(st->overrun_count, 0u);
    CHECK_EQ_U32(st->crosstalk_count, 0u);
    CHECK(ltdc_reload_is_consistent());

    /* 没有 pending 时的 RR 记为 orphan，而不是伪造一次 LVGL 完成 */
    CHECK(ltdc_reload_complete_from_irq() == LTDC_RELOAD_OWNER_NONE);
    CHECK_EQ_U32(ltdc_reload_state()->orphan_completions, 1u);
    CHECK(!ltdc_reload_is_consistent());
}

static void test_reload_overrun_and_abandon(void)
{
    reset_world();

    CHECK(ltdc_reload_arm(LTDC_RELOAD_OWNER_LVGL_FLUSH) != 0u);
    /* 第二次 arm 必须被拒绝，避免两个提交者共用一次 RR */
    CHECK_EQ_U32(ltdc_reload_arm(LTDC_RELOAD_OWNER_LAUNCHER_PAN), 0u);
    CHECK_EQ_U32(ltdc_reload_state()->overrun_count, 1u);
    CHECK(ltdc_reload_pending_owner() == LTDC_RELOAD_OWNER_LVGL_FLUSH);

    /* 用错误的 owner 放弃会被记成串线且不生效 */
    CHECK(!ltdc_reload_abandon(LTDC_RELOAD_OWNER_LAUNCHER_PAN));
    CHECK_EQ_U32(ltdc_reload_state()->crosstalk_count, 1u);
    CHECK(ltdc_reload_is_pending());

    CHECK(ltdc_reload_abandon(LTDC_RELOAD_OWNER_LVGL_FLUSH));
    CHECK(!ltdc_reload_is_pending());

    CHECK(ltdc_reload_owner_name(LTDC_RELOAD_OWNER_LAUNCHER_PAN)[0] == 'L');
    CHECK(ltdc_reload_owner_name(LTDC_RELOAD_OWNER_NONE)[0] == 'N');
}

static ltdc_reload_owner_t g_dispatched_owner;
static uint32_t g_dispatched_generation;
static uint32_t g_dispatch_count;

static void fake_external_dispatch(ltdc_reload_owner_t owner,
                                   uint32_t generation,
                                   void *user)
{
    (void)user;
    g_dispatched_owner = owner;
    g_dispatched_generation = generation;
    ++g_dispatch_count;
}

static void test_external_dispatch(void)
{
    reset_world();
    g_dispatched_owner = LTDC_RELOAD_OWNER_NONE;
    g_dispatched_generation = 0u;
    g_dispatch_count = 0u;

    /* 未注册消费者：只计入 unhandled（启动期预期），不算一致性错误 */
    CHECK(!ltdc_reload_notify_external(LTDC_RELOAD_OWNER_MODE_SWITCH, 7u));
    CHECK_EQ_U32(ltdc_reload_unhandled_external(), 1u);
    CHECK(ltdc_reload_is_consistent());

    ltdc_reload_set_external_dispatch(fake_external_dispatch, NULL);
    CHECK(ltdc_reload_notify_external(LTDC_RELOAD_OWNER_MODE_SWITCH, 8u));
    CHECK(g_dispatched_owner == LTDC_RELOAD_OWNER_MODE_SWITCH);
    CHECK_EQ_U32(g_dispatched_generation, 8u);
    CHECK_EQ_U32(g_dispatch_count, 1u);
    CHECK(ltdc_reload_is_consistent());

    ltdc_reload_set_external_dispatch(NULL, NULL);
}

/* ------------------------------------------------------------ mode 状态机 */

static void test_display_mode(void)
{
    display_mode_state_t st;
    display_mode_init(&st);

    CHECK(st.mode == DISPLAY_MODE_LVGL_APP);
    CHECK(display_mode_is_stable(&st));
    CHECK(display_mode_lvgl_refresh_allowed(&st));
    CHECK(!display_mode_hw_pan_active(&st));

    /* 有未完成 reload 时拒绝切换 */
    CHECK(!display_mode_request(&st, DISPLAY_MODE_LAUNCHER_HW_PAN, true));
    CHECK_EQ_U32(st.rejected_reload, 1u);
    CHECK(st.mode == DISPLAY_MODE_LVGL_APP);

    /* 合法请求进入过渡态，此时 LVGL 仍允许刷新（还没关 invalidation） */
    CHECK(display_mode_request(&st, DISPLAY_MODE_LAUNCHER_HW_PAN, false));
    CHECK(st.mode == DISPLAY_MODE_TO_LAUNCHER_HW_PAN);
    CHECK(!display_mode_is_stable(&st));
    CHECK(display_mode_lvgl_refresh_allowed(&st));
    CHECK(!display_mode_hw_pan_active(&st));

    /* 过渡中不允许再次请求 */
    CHECK(!display_mode_request(&st, DISPLAY_MODE_LVGL_APP, false));
    CHECK_EQ_U32(st.rejected_busy, 1u);

    /* 非法目标被拒绝 */
    CHECK(!display_mode_request(&st, DISPLAY_MODE_TO_LVGL_APP, false));
    CHECK_EQ_U32(st.rejected_invalid, 1u);

    CHECK(display_mode_commit(&st));
    CHECK(st.mode == DISPLAY_MODE_LAUNCHER_HW_PAN);
    CHECK(display_mode_hw_pan_active(&st));
    CHECK(!display_mode_lvgl_refresh_allowed(&st));

    /* 回到 Lua：过渡态期间仍然禁止 LVGL 刷新，避免出现半套几何 */
    CHECK(display_mode_request(&st, DISPLAY_MODE_LVGL_APP, false));
    CHECK(st.mode == DISPLAY_MODE_TO_LVGL_APP);
    CHECK(!display_mode_hw_pan_active(&st));
    CHECK(display_mode_lvgl_refresh_allowed(&st));

    CHECK(display_mode_commit(&st));
    CHECK(st.mode == DISPLAY_MODE_LVGL_APP);
    CHECK(display_mode_lvgl_refresh_allowed(&st));
    CHECK_EQ_U32(st.request_count, 2u);
    CHECK_EQ_U32(st.commit_count, 2u);

    /* abort 回到稳定态 */
    display_mode_state_t ab;
    display_mode_init(&ab);
    CHECK(display_mode_request(&ab, DISPLAY_MODE_LAUNCHER_HW_PAN, false));
    CHECK(display_mode_abort(&ab, DISPLAY_MODE_LVGL_APP));
    CHECK(ab.mode == DISPLAY_MODE_LVGL_APP);
    CHECK_EQ_U32(ab.abort_count, 1u);
    CHECK(!display_mode_abort(&ab, DISPLAY_MODE_LVGL_APP));

    /* 幂等请求：已经在目标模式时不产生过渡 */
    display_mode_state_t idem;
    display_mode_init(&idem);
    CHECK(display_mode_request(&idem, DISPLAY_MODE_LVGL_APP, false));
    CHECK(display_mode_is_stable(&idem));
    CHECK(idem.mode == DISPLAY_MODE_LVGL_APP);

    CHECK(display_mode_name(DISPLAY_MODE_LAUNCHER_HW_PAN)[0] == 'L');
}

/* ------------------------------------------------ mode switch + pan 组合 */

static void test_mode_switch_stress(void)
{
    reset_world();

    display_mode_state_t st;
    display_mode_init(&st);

    launcher_hw_pan_t pan;
    launcher_hw_pan_init(&pan, &k_geo, &k_ops, 0);

    const uint32_t rounds = 50u;
    for (uint32_t i = 0u; i < rounds; ++i) {
        /* Launcher -> HW pan */
        CHECK(display_mode_request(&st, DISPLAY_MODE_LAUNCHER_HW_PAN, false));
        (void)display_mode_commit(&st);
        launcher_hw_pan_set_enabled(&pan, true);

        /* pan 一小段并等完成 */
        (void)launcher_hw_pan_set_x(&pan, (int32_t)(i % 1861u));
        if (pan.pending) {
            complete_reload_event(&pan);
        }
        CHECK(launcher_hw_pan_is_settled(&pan));

        /* 回到 Lua */
        launcher_hw_pan_set_enabled(&pan, false);
        CHECK(display_mode_request(&st, DISPLAY_MODE_LVGL_APP, false));
        (void)display_mode_commit(&st);

        CHECK(!ltdc_reload_is_pending());
        CHECK(pan.generation_mismatch == 0u);
        CHECK(pan.vbr_rejected == 0u);
    }

    CHECK_EQ_U32(st.commit_count, rounds * 2u);
    CHECK(st.mode == DISPLAY_MODE_LVGL_APP);
    CHECK(ltdc_reload_is_consistent());
    CHECK(pan.desired_x == pan.latched_x);
}

int main(void)
{
    test_geometry();
    test_source_address();
    test_pan_submit_and_complete();
    test_pan_bounds_and_latest_wins();
    test_pan_disable_and_reject();
    test_generation_mismatch();
    test_reload_owner_routing();
    test_reload_overrun_and_abandon();
    test_external_dispatch();
    test_display_mode();
    test_mode_switch_stress();

    printf("\n[LAUNCHER HW PAN TEST] checks=%u failures=%u %s\n",
           g_checks, g_failures, g_failures == 0u ? "PASS" : "FAIL");

    return g_failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
