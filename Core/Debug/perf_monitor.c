#include "perf_monitor.h"

#if PERF_MONITOR_ENABLE

#include <limits.h>

#include "main.h"

volatile PerfMonitorStats g_perf_monitor_stats[PERF_MONITOR_POINT_COUNT];

void PerfMonitor_Reset(void)
{
    for (uint32_t i = 0u; i < (uint32_t)PERF_MONITOR_POINT_COUNT; ++i) {
        g_perf_monitor_stats[i].count = 0u;
        g_perf_monitor_stats[i].min_cycles = UINT32_MAX;
        g_perf_monitor_stats[i].max_cycles = 0u;
        g_perf_monitor_stats[i].total_cycles = 0u;
    }
}

void PerfMonitor_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    PerfMonitor_Reset();
}

uint32_t PerfMonitor_Begin(void)
{
    return DWT->CYCCNT;
}

void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle)
{
    uint32_t elapsed;
    volatile PerfMonitorStats *stats;

    if ((uint32_t)point >= (uint32_t)PERF_MONITOR_POINT_COUNT) {
        return;
    }

    /* Unsigned subtraction remains valid when CYCCNT wraps once. */
    elapsed = DWT->CYCCNT - start_cycle;
    stats = &g_perf_monitor_stats[point];
    stats->count += 1u;
    stats->total_cycles += (uint64_t)elapsed;
    if (elapsed < stats->min_cycles) {
        stats->min_cycles = elapsed;
    }
    if (elapsed > stats->max_cycles) {
        stats->max_cycles = elapsed;
    }
}

#endif
