#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PERF_MONITOR_ENABLE
#define PERF_MONITOR_ENABLE 0
#endif

typedef enum {
    PERF_MONITOR_IRQ_SYSTICK = 0,
    PERF_MONITOR_IRQ_EXTI3,
    PERF_MONITOR_IRQ_SDMMC1,
    PERF_MONITOR_IRQ_USB_OTG_HS,
    PERF_MONITOR_IRQ_LTDC,
    PERF_MONITOR_IRQ_DMA2D,
    PERF_MONITOR_IRQ_TIM16,
    PERF_MONITOR_IRQ_MDMA,
    PERF_MONITOR_POINT_COUNT
} PerfMonitorPoint;

typedef struct {
    uint32_t count;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
} PerfMonitorStats;

#if PERF_MONITOR_ENABLE

extern volatile PerfMonitorStats g_perf_monitor_stats[PERF_MONITOR_POINT_COUNT];

void PerfMonitor_Init(void);
void PerfMonitor_Reset(void);
uint32_t PerfMonitor_Begin(void);
void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle);

#else

static inline void PerfMonitor_Init(void)
{
}

static inline void PerfMonitor_Reset(void)
{
}

static inline uint32_t PerfMonitor_Begin(void)
{
    return 0u;
}

static inline void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle)
{
    (void)point;
    (void)start_cycle;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PERF_MONITOR_H */
