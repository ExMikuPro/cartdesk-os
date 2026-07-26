#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdbool.h>
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
    PERF_MONITOR_IRQ_MDMA,
    PERF_MONITOR_STARTUP_QFLASH_INIT,
    PERF_MONITOR_STARTUP_QFLASH_MOUNT,
    PERF_MONITOR_STARTUP_FONT_HEADER,
    PERF_MONITOR_STARTUP_FONT_VERSION,
    PERF_MONITOR_STARTUP_FONT_VALIDATE,
    PERF_MONITOR_STARTUP_SDMMC_INIT,
    PERF_MONITOR_STARTUP_FATFS_MOUNT,
    PERF_MONITOR_STARTUP_CART_HEADER,
    PERF_MONITOR_STARTUP_LAUNCHER_SCAN,
    PERF_MONITOR_STARTUP_TITLE_READ,
    PERF_MONITOR_STARTUP_PREVIEW_READ,
    PERF_MONITOR_STARTUP_PREVIEW_CONVERT,
    PERF_MONITOR_STARTUP_LAUNCHER_OBJECTS,
    PERF_MONITOR_STARTUP_FIRST_LV_TIMER,
    PERF_MONITOR_STARTUP_FIRST_FLUSH_SUBMIT,
    PERF_MONITOR_STARTUP_FIRST_FLUSH_WAIT,
    PERF_MONITOR_STARTUP_FIRST_PAGE_FLIP,
    PERF_MONITOR_STARTUP_FIRST_SCREEN_VISIBLE,
    PERF_MONITOR_RUNTIME_LUA_UPDATE,
    PERF_MONITOR_RUNTIME_SD_READ,
    PERF_MONITOR_POINT_COUNT
} PerfMonitorPoint;

typedef struct {
    uint32_t count;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
} PerfMonitorStats;

#define PERF_MONITOR_SD_TRACE_CAPACITY 64u

typedef struct {
    uint64_t offset_bytes;
    uint32_t length_bytes;
    uint32_t buffer_addr;
    uint32_t buffer_alignment;
    uint32_t cache_addr;
    uint32_t cache_length;
    uint32_t elapsed_cycles;
    int32_t status;
    uint8_t used_dma;
    uint8_t used_bounce;
    uint8_t reserved[2];
} PerfMonitorSdReadTrace;

#if PERF_MONITOR_ENABLE

extern volatile PerfMonitorStats g_perf_monitor_stats[PERF_MONITOR_POINT_COUNT];
extern volatile PerfMonitorSdReadTrace g_perf_monitor_sd_reads[PERF_MONITOR_SD_TRACE_CAPACITY];
extern volatile uint32_t g_perf_monitor_sd_read_write_index;
extern volatile uint32_t g_perf_monitor_sd_read_count;
extern volatile uint32_t g_perf_monitor_first_screen_visible_ms;

void PerfMonitor_Init(void);
void PerfMonitor_Reset(void);
uint32_t PerfMonitor_Begin(void);
uint32_t PerfMonitor_ElapsedSinceInit(void);
void PerfMonitor_RecordFirstScreenVisible(void);
void PerfMonitor_Record(PerfMonitorPoint point, uint32_t elapsed_cycles);
void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle);
void PerfMonitor_RecordSdRead(uint64_t offset_bytes,
                              uint32_t length_bytes,
                              const void *buffer,
                              bool used_dma,
                              bool used_bounce,
                              uint32_t cache_addr,
                              uint32_t cache_length,
                              int32_t status,
                              uint32_t start_cycle);

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

static inline uint32_t PerfMonitor_ElapsedSinceInit(void)
{
    return 0u;
}

static inline void PerfMonitor_RecordFirstScreenVisible(void)
{
}

static inline void PerfMonitor_End(PerfMonitorPoint point, uint32_t start_cycle)
{
    (void)point;
    (void)start_cycle;
}

static inline void PerfMonitor_Record(PerfMonitorPoint point, uint32_t elapsed_cycles)
{
    (void)point;
    (void)elapsed_cycles;
}

static inline void PerfMonitor_RecordSdRead(uint64_t offset_bytes,
                                            uint32_t length_bytes,
                                            const void *buffer,
                                            bool used_dma,
                                            bool used_bounce,
                                            uint32_t cache_addr,
                                            uint32_t cache_length,
                                            int32_t status,
                                            uint32_t start_cycle)
{
    (void)offset_bytes;
    (void)length_bytes;
    (void)buffer;
    (void)used_dma;
    (void)used_bounce;
    (void)cache_addr;
    (void)cache_length;
    (void)status;
    (void)start_cycle;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PERF_MONITOR_H */
