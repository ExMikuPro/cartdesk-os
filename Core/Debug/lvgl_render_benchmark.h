#ifndef CARTDESK_LVGL_RENDER_BENCHMARK_H
#define CARTDESK_LVGL_RENDER_BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CARTDESK_RENDER_AUDIT_ENABLE

enum {
    LVGL_RENDER_BENCH_COMMAND_NONE = 0u,
    LVGL_RENDER_BENCH_COMMAND_L0 = 1u,
    LVGL_RENDER_BENCH_COMMAND_L1 = 2u,
    LVGL_RENDER_BENCH_COMMAND_L2 = 3u,
    LVGL_RENDER_BENCH_COMMAND_L3_XRGB = 4u,
    LVGL_RENDER_BENCH_COMMAND_L4_ARGB = 5u,
    LVGL_RENDER_BENCH_COMMAND_L5_ALPHA = 6u
};

enum {
    LVGL_RENDER_BENCH_STATE_IDLE = 0u,
    LVGL_RENDER_BENCH_STATE_READY = 1u,
    LVGL_RENDER_BENCH_STATE_RUNNING = 2u,
    LVGL_RENDER_BENCH_STATE_DONE = 3u,
    LVGL_RENDER_BENCH_STATE_ERROR = 4u
};

extern volatile uint32_t g_lvgl_render_bench_command;
extern volatile uint32_t g_lvgl_render_bench_run;
extern volatile uint32_t g_lvgl_render_bench_state;
extern volatile uint32_t g_lvgl_render_bench_scene;
extern volatile uint32_t g_lvgl_render_bench_step;

/* Returns true while the debug benchmark owns the active LVGL screen. */
bool LvglRenderBenchmark_Poll(void);

#else

static inline bool LvglRenderBenchmark_Poll(void)
{
    return false;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
