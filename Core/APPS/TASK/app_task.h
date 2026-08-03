#ifndef CARTDESK_APP_TASK_H
#define CARTDESK_APP_TASK_H

#include "task_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

void CartdeskAppTask_Run(void *argument);
void CartdeskAppTask_GetStats(cart_task_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
