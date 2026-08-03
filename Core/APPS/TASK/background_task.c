#include "background_task.h"

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "cart_log.h"
#include "runtime_stats.h"
#include "task.h"

static cart_task_stats_t s_stats;

void CartdeskBackgroundTask_GetStats(cart_task_stats_t *stats)
{
    if (stats != NULL) *stats = s_stats;
}

void CartdeskBackgroundTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        (void)CartLog_ProcessOne(1000u);
        RuntimeStats_PrintEveryMs(1000u);
        ++s_stats.heartbeat;
        s_stats.last_heartbeat_tick = osKernelGetTickCount();
        ++s_stats.processed;
        s_stats.queue_full = CartLog_DroppedCount();
        s_stats.max_queue_depth = CartLog_MaxQueueDepth();
        s_stats.stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
}
