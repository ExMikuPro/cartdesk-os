#include "perf_monitor.h"

#if PERF_MONITOR_ENABLE

#include <limits.h>

#include "main.h"

volatile PerfMonitorStats g_perf_monitor_stats[PERF_MONITOR_POINT_COUNT];
volatile PerfMonitorSdReadTrace g_perf_monitor_sd_reads[PERF_MONITOR_SD_TRACE_CAPACITY];
volatile uint32_t g_perf_monitor_sd_read_write_index;
volatile uint32_t g_perf_monitor_sd_read_count;
volatile uint32_t g_perf_monitor_first_screen_visible_ms;
static uint32_t s_perf_monitor_init_cycle;
static uint32_t s_perf_monitor_init_tick;

void PerfMonitor_Reset(void)
{
    for (uint32_t i = 0u; i < (uint32_t)PERF_MONITOR_POINT_COUNT; ++i) {
        g_perf_monitor_stats[i].count = 0u;
        g_perf_monitor_stats[i].last_cycles = 0u;
        g_perf_monitor_stats[i].min_cycles = UINT32_MAX;
        g_perf_monitor_stats[i].max_cycles = 0u;
        g_perf_monitor_stats[i].total_cycles = 0u;
    }
    g_perf_monitor_sd_read_write_index = 0u;
    g_perf_monitor_sd_read_count = 0u;
}

void PerfMonitor_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    s_perf_monitor_init_cycle = DWT->CYCCNT;
    s_perf_monitor_init_tick = HAL_GetTick();
    g_perf_monitor_first_screen_visible_ms = 0u;
    PerfMonitor_Reset();
}

uint32_t PerfMonitor_Begin(void)
{
    return DWT->CYCCNT;
}

uint32_t PerfMonitor_ElapsedSinceInit(void)
{
    return DWT->CYCCNT - s_perf_monitor_init_cycle;
}

void PerfMonitor_RecordFirstScreenVisible(void)
{
    g_perf_monitor_first_screen_visible_ms = HAL_GetTick() - s_perf_monitor_init_tick;
    PerfMonitor_Record(PERF_MONITOR_STARTUP_FIRST_SCREEN_VISIBLE,
                       PerfMonitor_ElapsedSinceInit());
}

void PerfMonitor_Record(PerfMonitorPoint point, uint32_t elapsed_cycles)
{
    volatile PerfMonitorStats *stats;

    if ((uint32_t)point >= (uint32_t)PERF_MONITOR_POINT_COUNT) {
        return;
    }

    stats = &g_perf_monitor_stats[point];
    stats->count += 1u;
    stats->last_cycles = elapsed_cycles;
    stats->total_cycles += (uint64_t)elapsed_cycles;
    if (elapsed_cycles < stats->min_cycles) {
        stats->min_cycles = elapsed_cycles;
    }
    if (elapsed_cycles > stats->max_cycles) {
        stats->max_cycles = elapsed_cycles;
    }
}

void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle)
{
    /* Unsigned subtraction remains valid when CYCCNT wraps once. */
    PerfMonitor_Record(point, DWT->CYCCNT - start_cycle);
}

void PerfMonitor_RecordSdRead(uint64_t offset_bytes,
                              uint32_t length_bytes,
                              const void *buffer,
                              bool used_dma,
                              bool used_bounce,
                              uint32_t cache_addr,
                              uint32_t cache_length,
                              int32_t status,
                              uint32_t start_cycle)
{
    uint32_t elapsed = DWT->CYCCNT - start_cycle;
    uint32_t index = g_perf_monitor_sd_read_write_index;
    volatile PerfMonitorSdReadTrace *trace = &g_perf_monitor_sd_reads[index];

    trace->offset_bytes = offset_bytes;
    trace->length_bytes = length_bytes;
    trace->buffer_addr = (uint32_t)(uintptr_t)buffer;
    trace->buffer_alignment = (uint32_t)((uintptr_t)buffer & 31u);
    trace->cache_addr = cache_addr;
    trace->cache_length = cache_length;
    trace->elapsed_cycles = elapsed;
    trace->status = status;
    trace->used_dma = used_dma ? 1u : 0u;
    trace->used_bounce = used_bounce ? 1u : 0u;

    index += 1u;
    if (index >= PERF_MONITOR_SD_TRACE_CAPACITY) {
        index = 0u;
    }
    g_perf_monitor_sd_read_write_index = index;
    if (g_perf_monitor_sd_read_count < PERF_MONITOR_SD_TRACE_CAPACITY) {
        g_perf_monitor_sd_read_count += 1u;
    }
    PerfMonitor_Record(PERF_MONITOR_RUNTIME_SD_READ, elapsed);
}

#endif
