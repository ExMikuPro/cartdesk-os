#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RUNTIME_STATS_ENABLE_LVGL_OVERLAY
#define RUNTIME_STATS_ENABLE_LVGL_OVERLAY 0
#endif

#ifndef RUNTIME_STATS_ENABLE_UART_PRINT
#define RUNTIME_STATS_ENABLE_UART_PRINT 1
#endif

typedef enum {
    RUNTIME_STATS_SECTION_LVGL = 0,
    RUNTIME_STATS_SECTION_LUA,
    RUNTIME_STATS_SECTION_LAUNCHER,
    RUNTIME_STATS_SECTION_FRAME,
    RUNTIME_STATS_SECTION_COUNT
} RuntimeStatsSection;

typedef struct {
    uint32_t last_us;
    uint32_t peak_us;
    uint64_t total_us;
    uint32_t count;
} RuntimeStatsTiming;

typedef struct {
    uint32_t uptime_ms;

    RuntimeStatsTiming lvgl;
    RuntimeStatsTiming lua;
    RuntimeStatsTiming launcher;
    RuntimeStatsTiming frame;
    RuntimeStatsTiming period;

    uint32_t frame_over_16ms_count;
    uint32_t frame_over_33ms_count;
    uint32_t frame_over_50ms_count;

    uint32_t lvgl_over_8ms_count;
    uint32_t lvgl_over_16ms_count;
    uint32_t lvgl_over_33ms_count;

    uint32_t lua_over_4ms_count;
    uint32_t lua_over_8ms_count;
    uint32_t lua_over_16ms_count;

    uint32_t launcher_over_4ms_count;
    uint32_t launcher_over_8ms_count;
    uint32_t launcher_over_16ms_count;

    uint32_t lua_heap_used;
    uint32_t lua_heap_peak;
    uint32_t lua_heap_global_peak;
    uint32_t lua_heap_capacity;
    uint32_t lua_alloc_fail_count;

    uint32_t resource_used;
    uint32_t resource_peak;
    uint32_t resource_global_peak;
    uint32_t resource_capacity;
    uint32_t resource_alive_count;
    uint32_t resource_indexed_count;
    uint32_t resource_refcount_anomaly_count;

    uint32_t input_queue_len;
    uint32_t input_queue_global_peak;
    uint32_t input_queue_capacity;
    uint32_t message_queue_len;
    uint32_t message_queue_global_peak;
    uint32_t message_queue_capacity;

    uint32_t freertos_heap_free;
    uint32_t current_task_stack_high_water;

    uint32_t lua_runtime_state;
} RuntimeStatsSnapshot;

void RuntimeStats_Init(void);
uint32_t RuntimeStats_NowUs(void);
void RuntimeStats_BeginSection(RuntimeStatsSection section);
void RuntimeStats_EndSection(RuntimeStatsSection section);
void RuntimeStats_UpdateSnapshot(void);
void RuntimeStats_GetSnapshot(RuntimeStatsSnapshot *out);
void RuntimeStats_ResetPeaks(void);
void RuntimeStats_ResetCounters(void);
void RuntimeStats_PrintEveryMs(uint32_t interval_ms);
void RuntimeStats_SetPrintEnabled(bool enabled);
bool RuntimeStats_IsPrintEnabled(void);
const char *RuntimeStats_LuaStateName(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_STATS_H */
