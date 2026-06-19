#include "runtime_stats.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "resource_manager.h"
#include "task.h"
#include "cartdesk_task.h"
#include "lua_vm.h"
#include "lua_vm_memory.h"

typedef enum {
    RUNTIME_STATS_LVGL_TIMING_TIMER = 0,
    RUNTIME_STATS_LVGL_TIMING_FLUSH,
    RUNTIME_STATS_LVGL_TIMING_FLUSH_WAIT,
    RUNTIME_STATS_LVGL_TIMING_DMA2D,
    RUNTIME_STATS_LVGL_TIMING_INPUT,
    RUNTIME_STATS_LVGL_TIMING_SCREEN,
    RUNTIME_STATS_LVGL_TIMING_COUNT
} RuntimeStatsLvglTimingIndex;

typedef struct {
    uint32_t section_start_tick[RUNTIME_STATS_SECTION_COUNT];
    uint32_t lvgl_timing_start_tick[RUNTIME_STATS_LVGL_TIMING_COUNT];
    uint8_t lvgl_timing_depth[RUNTIME_STATS_LVGL_TIMING_COUNT];
    RuntimeStatsSnapshot snapshot;
    uint32_t last_print_ms;
    uint32_t lvgl_slow_event_seq;
    uint32_t last_printed_lvgl_slow_event_seq;
    uint32_t last_frame_begin_tick;
    uint32_t last_time_tick;
    uint32_t time_cycle_remainder;
    uint32_t time_us_accum;
    uint8_t initialized;
    uint8_t dwt_ready;
    uint8_t dwt_inited;
    uint8_t print_enabled;
} RuntimeStatsState;

static RuntimeStatsState s_runtime_stats;

static uint32_t RuntimeStats_TicksToUs(uint32_t ticks);
static uint32_t RuntimeStats_ReadTick(void);
static RuntimeStatsTiming *RuntimeStats_LvglTimingSlot(RuntimeStatsLvglTimingIndex index);
static void RuntimeStats_ResetLvglFrameBreakdown(void);
static void RuntimeStats_AccumulateLvglTiming(RuntimeStatsLvglTimingIndex index, uint32_t elapsed_us);
static void RuntimeStats_BeginLvglTiming(RuntimeStatsLvglTimingIndex index);
static void RuntimeStats_EndLvglTiming(RuntimeStatsLvglTimingIndex index);
static uint32_t RuntimeStats_ClassifyLvglSlowReason(uint32_t lvgl_total_us);

static RuntimeStatsTiming *RuntimeStats_TimingSlot(RuntimeStatsSection section)
{
    switch (section) {
        case RUNTIME_STATS_SECTION_LVGL:
            return &s_runtime_stats.snapshot.lvgl;
        case RUNTIME_STATS_SECTION_LUA:
            return &s_runtime_stats.snapshot.lua;
        case RUNTIME_STATS_SECTION_LAUNCHER:
            return &s_runtime_stats.snapshot.launcher;
        case RUNTIME_STATS_SECTION_FRAME:
            return &s_runtime_stats.snapshot.frame;
        default:
            return NULL;
    }
}

static RuntimeStatsTiming *RuntimeStats_LvglTimingSlot(RuntimeStatsLvglTimingIndex index)
{
    switch (index) {
        case RUNTIME_STATS_LVGL_TIMING_TIMER:
            return &s_runtime_stats.snapshot.lvgl_timer;
        case RUNTIME_STATS_LVGL_TIMING_FLUSH:
            return &s_runtime_stats.snapshot.lvgl_flush;
        case RUNTIME_STATS_LVGL_TIMING_FLUSH_WAIT:
            return &s_runtime_stats.snapshot.lvgl_flush_wait;
        case RUNTIME_STATS_LVGL_TIMING_DMA2D:
            return &s_runtime_stats.snapshot.lvgl_dma2d;
        case RUNTIME_STATS_LVGL_TIMING_INPUT:
            return &s_runtime_stats.snapshot.lvgl_input;
        case RUNTIME_STATS_LVGL_TIMING_SCREEN:
            return &s_runtime_stats.snapshot.lvgl_screen;
        default:
            return NULL;
    }
}

static void RuntimeStats_ResetLvglFrameBreakdown(void)
{
    s_runtime_stats.snapshot.lvgl_timer.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_flush.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_flush_wait.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_dma2d.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_input.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_screen.last_us = 0u;
    s_runtime_stats.snapshot.lvgl_flush_count_last = 0u;
    s_runtime_stats.snapshot.lvgl_flush_px_last = 0u;
    s_runtime_stats.snapshot.lvgl_input_read_count_last = 0u;
    s_runtime_stats.snapshot.lvgl_slow_reason = RUNTIME_STATS_LVGL_SLOW_NONE;
}

static void RuntimeStats_AccumulateLvglTiming(RuntimeStatsLvglTimingIndex index, uint32_t elapsed_us)
{
    RuntimeStatsTiming *timing = RuntimeStats_LvglTimingSlot(index);
    if (timing == NULL) {
        return;
    }

    timing->last_us += elapsed_us;
    if (timing->last_us > timing->peak_us) {
        timing->peak_us = timing->last_us;
    }
    timing->total_us += (uint64_t)elapsed_us;
    timing->count += 1u;
}

static void RuntimeStats_BeginLvglTiming(RuntimeStatsLvglTimingIndex index)
{
    if (index >= RUNTIME_STATS_LVGL_TIMING_COUNT) {
        return;
    }
    if (s_runtime_stats.initialized == 0u) {
        RuntimeStats_Init();
    }
    if (s_runtime_stats.lvgl_timing_depth[index] == 0u) {
        s_runtime_stats.lvgl_timing_start_tick[index] = RuntimeStats_ReadTick();
    }
    if (s_runtime_stats.lvgl_timing_depth[index] != UINT8_MAX) {
        s_runtime_stats.lvgl_timing_depth[index] += 1u;
    }
}

static void RuntimeStats_EndLvglTiming(RuntimeStatsLvglTimingIndex index)
{
    uint32_t elapsed_tick;
    uint32_t elapsed_us;

    if (index >= RUNTIME_STATS_LVGL_TIMING_COUNT) {
        return;
    }
    if (s_runtime_stats.lvgl_timing_depth[index] == 0u) {
        return;
    }

    s_runtime_stats.lvgl_timing_depth[index] -= 1u;
    if (s_runtime_stats.lvgl_timing_depth[index] != 0u) {
        return;
    }

    elapsed_tick = RuntimeStats_ReadTick() - s_runtime_stats.lvgl_timing_start_tick[index];
    elapsed_us = RuntimeStats_TicksToUs(elapsed_tick);
    RuntimeStats_AccumulateLvglTiming(index, elapsed_us);
}

static uint32_t RuntimeStats_ClassifyLvglSlowReason(uint32_t lvgl_total_us)
{
    uint32_t flush_wait_us = s_runtime_stats.snapshot.lvgl_flush_wait.last_us;
    uint32_t flush_us = s_runtime_stats.snapshot.lvgl_flush.last_us;
    uint32_t dma2d_us = s_runtime_stats.snapshot.lvgl_dma2d.last_us;
    uint32_t timer_us = s_runtime_stats.snapshot.lvgl_timer.last_us;
    uint32_t input_us = s_runtime_stats.snapshot.lvgl_input.last_us;
    uint32_t screen_us = s_runtime_stats.snapshot.lvgl_screen.last_us;

    if (flush_wait_us >= flush_us && flush_wait_us >= dma2d_us && flush_wait_us >= timer_us &&
        flush_wait_us >= input_us && flush_wait_us >= screen_us && flush_wait_us > 8000u) {
        return RUNTIME_STATS_LVGL_SLOW_FLUSH_WAIT;
    }
    if (flush_us >= dma2d_us && flush_us >= timer_us && flush_us >= input_us &&
        flush_us >= screen_us && flush_us > 8000u) {
        return RUNTIME_STATS_LVGL_SLOW_FLUSH;
    }
    if (dma2d_us >= timer_us && dma2d_us >= input_us && dma2d_us >= screen_us && dma2d_us > 8000u) {
        return RUNTIME_STATS_LVGL_SLOW_DMA2D;
    }
    if (timer_us >= input_us && timer_us >= screen_us && timer_us > 8000u) {
        return RUNTIME_STATS_LVGL_SLOW_TIMER;
    }
    if (input_us >= screen_us && input_us > 4000u) {
        return RUNTIME_STATS_LVGL_SLOW_INPUT;
    }
    if (screen_us > 4000u) {
        return RUNTIME_STATS_LVGL_SLOW_SCREEN;
    }
    if (lvgl_total_us > 8000u) {
        return RUNTIME_STATS_LVGL_SLOW_UNKNOWN;
    }
    return RUNTIME_STATS_LVGL_SLOW_NONE;
}

static void RuntimeStats_CountSlowFrame(uint32_t elapsed_us)
{
    if (elapsed_us > 16666u) {
        s_runtime_stats.snapshot.frame_over_16ms_count += 1u;
    }
    if (elapsed_us > 33333u) {
        s_runtime_stats.snapshot.frame_over_33ms_count += 1u;
    }
    if (elapsed_us > 50000u) {
        s_runtime_stats.snapshot.frame_over_50ms_count += 1u;
    }
}

static void RuntimeStats_CountSlowLvgl(uint32_t elapsed_us)
{
    if (elapsed_us > 8000u) {
        s_runtime_stats.snapshot.lvgl_over_8ms_count += 1u;
    }
    if (elapsed_us > 16666u) {
        s_runtime_stats.snapshot.lvgl_over_16ms_count += 1u;
    }
    if (elapsed_us > 33333u) {
        s_runtime_stats.snapshot.lvgl_over_33ms_count += 1u;
    }
}

static void RuntimeStats_CountSlowLua(uint32_t elapsed_us)
{
    if (elapsed_us > 4000u) {
        s_runtime_stats.snapshot.lua_over_4ms_count += 1u;
    }
    if (elapsed_us > 8000u) {
        s_runtime_stats.snapshot.lua_over_8ms_count += 1u;
    }
    if (elapsed_us > 16666u) {
        s_runtime_stats.snapshot.lua_over_16ms_count += 1u;
    }
}

static void RuntimeStats_CountSlowLauncher(uint32_t elapsed_us)
{
    if (elapsed_us > 4000u) {
        s_runtime_stats.snapshot.launcher_over_4ms_count += 1u;
    }
    if (elapsed_us > 8000u) {
        s_runtime_stats.snapshot.launcher_over_8ms_count += 1u;
    }
    if (elapsed_us > 16666u) {
        s_runtime_stats.snapshot.launcher_over_16ms_count += 1u;
    }
}

static void RuntimeStats_UpdatePeriod(uint32_t begin_tick)
{
    RuntimeStatsTiming *period = &s_runtime_stats.snapshot.period;
    uint32_t elapsed_us;

    if (s_runtime_stats.last_frame_begin_tick == 0u) {
        s_runtime_stats.last_frame_begin_tick = begin_tick;
        return;
    }

    elapsed_us = RuntimeStats_TicksToUs(begin_tick - s_runtime_stats.last_frame_begin_tick);
    s_runtime_stats.last_frame_begin_tick = begin_tick;
    period->last_us = elapsed_us;
    if (elapsed_us > period->peak_us) {
        period->peak_us = elapsed_us;
    }
    period->total_us += (uint64_t)elapsed_us;
    period->count += 1u;
}

static void RuntimeStats_InitDwtIfNeeded(void)
{
    if (s_runtime_stats.dwt_inited) {
        return;
    }

    s_runtime_stats.dwt_inited = 1u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_runtime_stats.dwt_ready = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u) ? 1u : 0u;
}

static uint32_t RuntimeStats_CyclesPerUs(void)
{
    return (uint32_t)(SystemCoreClock / 1000000u);
}

static uint32_t RuntimeStats_ReadTick(void)
{
    RuntimeStats_InitDwtIfNeeded();
    if (s_runtime_stats.dwt_ready != 0u) {
        return DWT->CYCCNT;
    }
    return HAL_GetTick() * 1000u;
}

static uint32_t RuntimeStats_TicksToUs(uint32_t ticks)
{
    if (s_runtime_stats.dwt_ready == 0u) {
        return ticks;
    }

    uint32_t cycles_per_us = RuntimeStats_CyclesPerUs();
    if (cycles_per_us == 0u) {
        return 0u;
    }
    return ticks / cycles_per_us;
}

static uint32_t RuntimeStats_SaturateSize(size_t value)
{
    return value > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static uint32_t RuntimeStats_ReadFreeRtosHeapFree(void)
{
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    return (uint32_t)xPortGetFreeHeapSize();
#else
    /* currently unavailable: FreeRTOS dynamic allocation is disabled. */
    return 0u;
#endif
}

static uint32_t RuntimeStats_ReadCurrentTaskStackHighWater(void)
{
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);
#elif defined(INCLUDE_uxTaskGetStackHighWaterMark2) && (INCLUDE_uxTaskGetStackHighWaterMark2 == 1)
    return (uint32_t)uxTaskGetStackHighWaterMark2(NULL) * (uint32_t)sizeof(StackType_t);
#else
    /* currently unavailable: stack high-water API is not enabled. */
    return 0u;
#endif
}

void RuntimeStats_Init(void)
{
    memset(&s_runtime_stats, 0, sizeof(s_runtime_stats));
    s_runtime_stats.initialized = 1u;
    s_runtime_stats.print_enabled = (RUNTIME_STATS_ENABLE_UART_PRINT != 0) ? 1u : 0u;
    RuntimeStats_InitDwtIfNeeded();
    s_runtime_stats.last_time_tick = RuntimeStats_ReadTick();
}

uint32_t RuntimeStats_NowUs(void)
{
    uint32_t now_tick;
    uint32_t delta_tick;

    if (s_runtime_stats.initialized == 0u) {
        RuntimeStats_Init();
    }

    now_tick = RuntimeStats_ReadTick();
    delta_tick = now_tick - s_runtime_stats.last_time_tick;
    s_runtime_stats.last_time_tick = now_tick;

    if (s_runtime_stats.dwt_ready == 0u) {
        s_runtime_stats.time_us_accum += delta_tick;
        return s_runtime_stats.time_us_accum;
    }

    uint32_t cycles_per_us = RuntimeStats_CyclesPerUs();
    if (cycles_per_us == 0u) {
        return s_runtime_stats.time_us_accum;
    }

    delta_tick += s_runtime_stats.time_cycle_remainder;
    s_runtime_stats.time_us_accum += delta_tick / cycles_per_us;
    s_runtime_stats.time_cycle_remainder = delta_tick % cycles_per_us;
    return s_runtime_stats.time_us_accum;
}

void RuntimeStats_BeginSection(RuntimeStatsSection section)
{
    if (section >= RUNTIME_STATS_SECTION_COUNT) {
        return;
    }
    if (s_runtime_stats.initialized == 0u) {
        RuntimeStats_Init();
    }
    if (section == RUNTIME_STATS_SECTION_LVGL) {
        RuntimeStats_ResetLvglFrameBreakdown();
    }
    s_runtime_stats.section_start_tick[section] = RuntimeStats_ReadTick();
    if (section == RUNTIME_STATS_SECTION_FRAME) {
        RuntimeStats_UpdatePeriod(s_runtime_stats.section_start_tick[section]);
    }
}

void RuntimeStats_EndSection(RuntimeStatsSection section)
{
    RuntimeStatsTiming *timing;
    uint32_t start_tick;
    uint32_t elapsed_us;
    uint32_t elapsed_tick;

    if (section >= RUNTIME_STATS_SECTION_COUNT) {
        return;
    }

    timing = RuntimeStats_TimingSlot(section);
    if (timing == NULL) {
        return;
    }

    start_tick = s_runtime_stats.section_start_tick[section];
    elapsed_tick = RuntimeStats_ReadTick() - start_tick;
    elapsed_us = RuntimeStats_TicksToUs(elapsed_tick);
    timing->last_us = elapsed_us;
    if (elapsed_us > timing->peak_us) {
        timing->peak_us = elapsed_us;
    }
    timing->total_us += (uint64_t)elapsed_us;
    timing->count += 1u;

    switch (section) {
        case RUNTIME_STATS_SECTION_LVGL:
            RuntimeStats_CountSlowLvgl(elapsed_us);
            s_runtime_stats.snapshot.lvgl_slow_reason = RuntimeStats_ClassifyLvglSlowReason(elapsed_us);
            if (elapsed_us > 16000u) {
                s_runtime_stats.lvgl_slow_event_seq += 1u;
                s_runtime_stats.snapshot.lvgl_slow_last_total_us = elapsed_us;
                s_runtime_stats.snapshot.lvgl_slow_last_timer_us = s_runtime_stats.snapshot.lvgl_timer.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_flush_us = s_runtime_stats.snapshot.lvgl_flush.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_flush_wait_us = s_runtime_stats.snapshot.lvgl_flush_wait.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_dma2d_us = s_runtime_stats.snapshot.lvgl_dma2d.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_input_us = s_runtime_stats.snapshot.lvgl_input.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_screen_us = s_runtime_stats.snapshot.lvgl_screen.last_us;
                s_runtime_stats.snapshot.lvgl_slow_last_flush_count = s_runtime_stats.snapshot.lvgl_flush_count_last;
                s_runtime_stats.snapshot.lvgl_slow_last_flush_px = s_runtime_stats.snapshot.lvgl_flush_px_last;
                s_runtime_stats.snapshot.lvgl_slow_last_reason = s_runtime_stats.snapshot.lvgl_slow_reason;
            }
            break;
        case RUNTIME_STATS_SECTION_LUA:
            RuntimeStats_CountSlowLua(elapsed_us);
            break;
        case RUNTIME_STATS_SECTION_LAUNCHER:
            RuntimeStats_CountSlowLauncher(elapsed_us);
            break;
        case RUNTIME_STATS_SECTION_FRAME:
            RuntimeStats_CountSlowFrame(elapsed_us);
            break;
        default:
            break;
    }
}

void RuntimeStats_BeginLvglTimer(void)
{
    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_TIMER);
}

void RuntimeStats_EndLvglTimer(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_TIMER);
}

void RuntimeStats_BeginLvglFlush(uint32_t area_px)
{
    if (s_runtime_stats.initialized == 0u) {
        RuntimeStats_Init();
    }

    s_runtime_stats.snapshot.lvgl_flush_count_last += 1u;
    if (s_runtime_stats.snapshot.lvgl_flush_count_last > s_runtime_stats.snapshot.lvgl_flush_count_peak) {
        s_runtime_stats.snapshot.lvgl_flush_count_peak = s_runtime_stats.snapshot.lvgl_flush_count_last;
    }
    s_runtime_stats.snapshot.lvgl_flush_count_total += 1u;

    s_runtime_stats.snapshot.lvgl_flush_px_last += area_px;
    if (s_runtime_stats.snapshot.lvgl_flush_px_last > s_runtime_stats.snapshot.lvgl_flush_px_peak) {
        s_runtime_stats.snapshot.lvgl_flush_px_peak = s_runtime_stats.snapshot.lvgl_flush_px_last;
    }
    s_runtime_stats.snapshot.lvgl_flush_px_total += (uint64_t)area_px;

    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_FLUSH);
}

void RuntimeStats_EndLvglFlush(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_FLUSH);
}

void RuntimeStats_BeginLvglFlushWait(void)
{
    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_FLUSH_WAIT);
}

void RuntimeStats_EndLvglFlushWait(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_FLUSH_WAIT);
}

void RuntimeStats_BeginLvglDma2d(void)
{
    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_DMA2D);
}

void RuntimeStats_EndLvglDma2d(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_DMA2D);
}

void RuntimeStats_BeginLvglInput(void)
{
    if (s_runtime_stats.initialized == 0u) {
        RuntimeStats_Init();
    }

    s_runtime_stats.snapshot.lvgl_input_read_count_last += 1u;
    if (s_runtime_stats.snapshot.lvgl_input_read_count_last > s_runtime_stats.snapshot.lvgl_input_read_count_peak) {
        s_runtime_stats.snapshot.lvgl_input_read_count_peak = s_runtime_stats.snapshot.lvgl_input_read_count_last;
    }
    s_runtime_stats.snapshot.lvgl_input_read_count_total += 1u;

    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_INPUT);
}

void RuntimeStats_EndLvglInput(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_INPUT);
}

void RuntimeStats_BeginLvglScreenOp(void)
{
    RuntimeStats_BeginLvglTiming(RUNTIME_STATS_LVGL_TIMING_SCREEN);
}

void RuntimeStats_EndLvglScreenOp(void)
{
    RuntimeStats_EndLvglTiming(RUNTIME_STATS_LVGL_TIMING_SCREEN);
}

void RuntimeStats_UpdateSnapshot(void)
{
    LuaVmMemoryStats memory_stats;

    memset(&memory_stats, 0, sizeof(memory_stats));
    lua_vm_memory_get_stats(&memory_stats);

    s_runtime_stats.snapshot.uptime_ms = HAL_GetTick();
    s_runtime_stats.snapshot.lua_heap_used = RuntimeStats_SaturateSize(memory_stats.used);
    s_runtime_stats.snapshot.lua_heap_peak = RuntimeStats_SaturateSize(memory_stats.peak);
    s_runtime_stats.snapshot.lua_heap_capacity = RuntimeStats_SaturateSize(memory_stats.capacity);
    s_runtime_stats.snapshot.lua_alloc_fail_count = RuntimeStats_SaturateSize(memory_stats.alloc_fail_count);
    s_runtime_stats.snapshot.resource_used = res_manager_used_bytes();
    s_runtime_stats.snapshot.resource_peak = res_manager_peak_bytes();
    s_runtime_stats.snapshot.resource_capacity = res_manager_capacity_bytes();
    s_runtime_stats.snapshot.resource_alive_count = res_manager_alive_count();
    s_runtime_stats.snapshot.resource_indexed_count = res_manager_indexed_count();
    s_runtime_stats.snapshot.resource_refcount_anomaly_count = res_manager_refcount_anomaly_count();
    s_runtime_stats.snapshot.input_queue_len = lua_vm_input_queue_len();
    s_runtime_stats.snapshot.input_queue_capacity = lua_vm_input_queue_capacity();
    s_runtime_stats.snapshot.message_queue_len = lua_vm_message_queue_len();
    s_runtime_stats.snapshot.message_queue_capacity = lua_vm_message_queue_capacity();
    s_runtime_stats.snapshot.freertos_heap_free = RuntimeStats_ReadFreeRtosHeapFree();
    s_runtime_stats.snapshot.current_task_stack_high_water = RuntimeStats_ReadCurrentTaskStackHighWater();
    s_runtime_stats.snapshot.lua_runtime_state = (uint32_t)Task_LUA_GetState();

    if (s_runtime_stats.snapshot.lua_heap_used > s_runtime_stats.snapshot.lua_heap_global_peak) {
        s_runtime_stats.snapshot.lua_heap_global_peak = s_runtime_stats.snapshot.lua_heap_used;
    }
    if (s_runtime_stats.snapshot.lua_heap_peak > s_runtime_stats.snapshot.lua_heap_global_peak) {
        s_runtime_stats.snapshot.lua_heap_global_peak = s_runtime_stats.snapshot.lua_heap_peak;
    }
    if (s_runtime_stats.snapshot.resource_used > s_runtime_stats.snapshot.resource_global_peak) {
        s_runtime_stats.snapshot.resource_global_peak = s_runtime_stats.snapshot.resource_used;
    }
    if (s_runtime_stats.snapshot.input_queue_len > s_runtime_stats.snapshot.input_queue_global_peak) {
        s_runtime_stats.snapshot.input_queue_global_peak = s_runtime_stats.snapshot.input_queue_len;
    }
    if (s_runtime_stats.snapshot.message_queue_len > s_runtime_stats.snapshot.message_queue_global_peak) {
        s_runtime_stats.snapshot.message_queue_global_peak = s_runtime_stats.snapshot.message_queue_len;
    }
}

void RuntimeStats_GetSnapshot(RuntimeStatsSnapshot *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_runtime_stats.snapshot;
}

void RuntimeStats_ResetPeaks(void)
{
    s_runtime_stats.snapshot.lvgl.peak_us = s_runtime_stats.snapshot.lvgl.last_us;
    s_runtime_stats.snapshot.lvgl_timer.peak_us = s_runtime_stats.snapshot.lvgl_timer.last_us;
    s_runtime_stats.snapshot.lvgl_flush.peak_us = s_runtime_stats.snapshot.lvgl_flush.last_us;
    s_runtime_stats.snapshot.lvgl_flush_wait.peak_us = s_runtime_stats.snapshot.lvgl_flush_wait.last_us;
    s_runtime_stats.snapshot.lvgl_dma2d.peak_us = s_runtime_stats.snapshot.lvgl_dma2d.last_us;
    s_runtime_stats.snapshot.lvgl_input.peak_us = s_runtime_stats.snapshot.lvgl_input.last_us;
    s_runtime_stats.snapshot.lvgl_screen.peak_us = s_runtime_stats.snapshot.lvgl_screen.last_us;
    s_runtime_stats.snapshot.lua.peak_us = s_runtime_stats.snapshot.lua.last_us;
    s_runtime_stats.snapshot.launcher.peak_us = s_runtime_stats.snapshot.launcher.last_us;
    s_runtime_stats.snapshot.frame.peak_us = s_runtime_stats.snapshot.frame.last_us;
    s_runtime_stats.snapshot.period.peak_us = s_runtime_stats.snapshot.period.last_us;
    s_runtime_stats.snapshot.lvgl_flush_count_peak = s_runtime_stats.snapshot.lvgl_flush_count_last;
    s_runtime_stats.snapshot.lvgl_flush_px_peak = s_runtime_stats.snapshot.lvgl_flush_px_last;
    s_runtime_stats.snapshot.lvgl_input_read_count_peak = s_runtime_stats.snapshot.lvgl_input_read_count_last;
    s_runtime_stats.snapshot.lua_heap_global_peak = s_runtime_stats.snapshot.lua_heap_used;
    s_runtime_stats.snapshot.resource_global_peak = s_runtime_stats.snapshot.resource_used;
    s_runtime_stats.snapshot.input_queue_global_peak = s_runtime_stats.snapshot.input_queue_len;
    s_runtime_stats.snapshot.message_queue_global_peak = s_runtime_stats.snapshot.message_queue_len;
}

void RuntimeStats_ResetCounters(void)
{
    s_runtime_stats.snapshot.frame_over_16ms_count = 0u;
    s_runtime_stats.snapshot.frame_over_33ms_count = 0u;
    s_runtime_stats.snapshot.frame_over_50ms_count = 0u;
    s_runtime_stats.snapshot.lvgl_over_8ms_count = 0u;
    s_runtime_stats.snapshot.lvgl_over_16ms_count = 0u;
    s_runtime_stats.snapshot.lvgl_over_33ms_count = 0u;
    s_runtime_stats.snapshot.lua_over_4ms_count = 0u;
    s_runtime_stats.snapshot.lua_over_8ms_count = 0u;
    s_runtime_stats.snapshot.lua_over_16ms_count = 0u;
    s_runtime_stats.snapshot.launcher_over_4ms_count = 0u;
    s_runtime_stats.snapshot.launcher_over_8ms_count = 0u;
    s_runtime_stats.snapshot.launcher_over_16ms_count = 0u;
}

void RuntimeStats_SetPrintEnabled(bool enabled)
{
    s_runtime_stats.print_enabled = enabled ? 1u : 0u;
}

bool RuntimeStats_IsPrintEnabled(void)
{
    return s_runtime_stats.print_enabled != 0u;
}

const char *RuntimeStats_LuaStateName(uint32_t state)
{
    return Task_LUA_GetStateName((TaskLuaState)state);
}

const char *RuntimeStats_LvglSlowReasonName(uint32_t reason)
{
    switch (reason) {
        case RUNTIME_STATS_LVGL_SLOW_NONE:
            return "NONE";
        case RUNTIME_STATS_LVGL_SLOW_TIMER:
            return "TIMER";
        case RUNTIME_STATS_LVGL_SLOW_FLUSH:
            return "FLUSH";
        case RUNTIME_STATS_LVGL_SLOW_FLUSH_WAIT:
            return "FLUSH_WAIT";
        case RUNTIME_STATS_LVGL_SLOW_DMA2D:
            return "DMA2D";
        case RUNTIME_STATS_LVGL_SLOW_INPUT:
            return "INPUT";
        case RUNTIME_STATS_LVGL_SLOW_SCREEN:
            return "SCREEN";
        case RUNTIME_STATS_LVGL_SLOW_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

void RuntimeStats_PrintEveryMs(uint32_t interval_ms)
{
    RuntimeStatsSnapshot snapshot;
    uint32_t now_ms;

    if (interval_ms == 0u) {
        return;
    }
    if (!RuntimeStats_IsPrintEnabled()) {
        return;
    }

    now_ms = HAL_GetTick();
    if (s_runtime_stats.last_print_ms != 0u &&
        (uint32_t)(now_ms - s_runtime_stats.last_print_ms) < interval_ms) {
        return;
    }
    s_runtime_stats.last_print_ms = now_ms;

    RuntimeStats_GetSnapshot(&snapshot);
    printf("[stats] up=%lums frame=%luus period=%luus lvgl=%luus lua=%luus launcher=%luus "
           "lv_timer=%luus flush=%luus flush_wait=%luus dma2d=%luus input_read=%luus screen=%luus "
           "flush_cnt=%lu flush_px=%lu input_cnt=%lu lvgl_reason=%lu/%s "
           "frame_peak=%luus period_peak=%luus lvgl_peak=%luus lua_peak=%luus launcher_peak=%luus "
           "slow=16:%lu,33:%lu,50:%lu lvgl_slow=8:%lu,16:%lu,33:%lu "
           "lua_slow=4:%lu,8:%lu,16:%lu launcher_slow=4:%lu,8:%lu,16:%lu "
           "lua_heap=%lu/%lu peak=%lu gpeak=%lu fail=%lu "
           "res=%lu/%lu peak=%lu gpeak=%lu alive=%lu indexed=%lu referr=%lu "
           "input=%lu/%lu ipeak=%lu msg=%lu/%lu mpeak=%lu state=%lu/%s stack=%lu heap=%lu\r\n",
           (unsigned long)snapshot.uptime_ms,
           (unsigned long)snapshot.frame.last_us,
           (unsigned long)snapshot.period.last_us,
           (unsigned long)snapshot.lvgl.last_us,
           (unsigned long)snapshot.lua.last_us,
           (unsigned long)snapshot.launcher.last_us,
           (unsigned long)snapshot.lvgl_timer.last_us,
           (unsigned long)snapshot.lvgl_flush.last_us,
           (unsigned long)snapshot.lvgl_flush_wait.last_us,
           (unsigned long)snapshot.lvgl_dma2d.last_us,
           (unsigned long)snapshot.lvgl_input.last_us,
           (unsigned long)snapshot.lvgl_screen.last_us,
           (unsigned long)snapshot.lvgl_flush_count_last,
           (unsigned long)snapshot.lvgl_flush_px_last,
           (unsigned long)snapshot.lvgl_input_read_count_last,
           (unsigned long)snapshot.lvgl_slow_reason,
           RuntimeStats_LvglSlowReasonName(snapshot.lvgl_slow_reason),
           (unsigned long)snapshot.frame.peak_us,
           (unsigned long)snapshot.period.peak_us,
           (unsigned long)snapshot.lvgl.peak_us,
           (unsigned long)snapshot.lua.peak_us,
           (unsigned long)snapshot.launcher.peak_us,
           (unsigned long)snapshot.frame_over_16ms_count,
           (unsigned long)snapshot.frame_over_33ms_count,
           (unsigned long)snapshot.frame_over_50ms_count,
           (unsigned long)snapshot.lvgl_over_8ms_count,
           (unsigned long)snapshot.lvgl_over_16ms_count,
           (unsigned long)snapshot.lvgl_over_33ms_count,
           (unsigned long)snapshot.lua_over_4ms_count,
           (unsigned long)snapshot.lua_over_8ms_count,
           (unsigned long)snapshot.lua_over_16ms_count,
           (unsigned long)snapshot.launcher_over_4ms_count,
           (unsigned long)snapshot.launcher_over_8ms_count,
           (unsigned long)snapshot.launcher_over_16ms_count,
           (unsigned long)snapshot.lua_heap_used,
           (unsigned long)snapshot.lua_heap_capacity,
           (unsigned long)snapshot.lua_heap_peak,
           (unsigned long)snapshot.lua_heap_global_peak,
           (unsigned long)snapshot.lua_alloc_fail_count,
           (unsigned long)snapshot.resource_used,
           (unsigned long)snapshot.resource_capacity,
           (unsigned long)snapshot.resource_peak,
           (unsigned long)snapshot.resource_global_peak,
           (unsigned long)snapshot.resource_alive_count,
           (unsigned long)snapshot.resource_indexed_count,
           (unsigned long)snapshot.resource_refcount_anomaly_count,
           (unsigned long)snapshot.input_queue_len,
           (unsigned long)snapshot.input_queue_capacity,
           (unsigned long)snapshot.input_queue_global_peak,
           (unsigned long)snapshot.message_queue_len,
           (unsigned long)snapshot.message_queue_capacity,
           (unsigned long)snapshot.message_queue_global_peak,
           (unsigned long)snapshot.lua_runtime_state,
           RuntimeStats_LuaStateName(snapshot.lua_runtime_state),
           (unsigned long)snapshot.current_task_stack_high_water,
           (unsigned long)snapshot.freertos_heap_free);

    if (snapshot.lvgl_slow_last_total_us > 16000u &&
        s_runtime_stats.lvgl_slow_event_seq != s_runtime_stats.last_printed_lvgl_slow_event_seq) {
        s_runtime_stats.last_printed_lvgl_slow_event_seq = s_runtime_stats.lvgl_slow_event_seq;
        printf("[lvgl-slow] total=%luus timer=%luus flush=%luus wait=%luus dma2d=%luus input=%luus screen=%luus "
               "cnt=%lu px=%lu reason=%lu/%s\r\n",
               (unsigned long)snapshot.lvgl_slow_last_total_us,
               (unsigned long)snapshot.lvgl_slow_last_timer_us,
               (unsigned long)snapshot.lvgl_slow_last_flush_us,
               (unsigned long)snapshot.lvgl_slow_last_flush_wait_us,
               (unsigned long)snapshot.lvgl_slow_last_dma2d_us,
               (unsigned long)snapshot.lvgl_slow_last_input_us,
               (unsigned long)snapshot.lvgl_slow_last_screen_us,
               (unsigned long)snapshot.lvgl_slow_last_flush_count,
               (unsigned long)snapshot.lvgl_slow_last_flush_px,
               (unsigned long)snapshot.lvgl_slow_last_reason,
               RuntimeStats_LvglSlowReasonName(snapshot.lvgl_slow_last_reason));
    }
}
