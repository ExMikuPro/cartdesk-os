#ifndef CARTDESK_BACKGROUND_TASK_H
#define CARTDESK_BACKGROUND_TASK_H

#include "task_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reserved for logging, integrity checks and other deferrable maintenance. */
void CartdeskBackgroundTask_Run(void *argument);
void CartdeskBackgroundTask_GetStats(cart_task_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
