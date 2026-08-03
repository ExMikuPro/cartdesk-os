#ifndef CARTDESK_AUDIO_TASK_H
#define CARTDESK_AUDIO_TASK_H

#include <stdbool.h>

#include "task_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CART_AUDIO_COMMAND_QUEUE_DEPTH 8u
#define CART_AUDIO_COMPLETION_QUEUE_DEPTH 8u

bool CartdeskAudioTask_Init(void);
bool CartdeskAudioTask_Submit(const cart_audio_command_t *command);
bool CartdeskAudioTask_TryReceive(cart_audio_completion_t *completion);
void CartdeskAudioTask_GetStats(cart_task_stats_t *stats);
void CartdeskAudioTask_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif
