#include "audio_task.h"

#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "stm32h743xx.h"

static osMessageQueueId_t s_command_queue;
static osMessageQueueId_t s_completion_queue;
static cart_task_stats_t s_stats;

bool CartdeskAudioTask_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_command_queue = osMessageQueueNew(CART_AUDIO_COMMAND_QUEUE_DEPTH,
                                        sizeof(cart_audio_command_t), NULL);
    s_completion_queue = osMessageQueueNew(CART_AUDIO_COMPLETION_QUEUE_DEPTH,
                                           sizeof(cart_audio_completion_t), NULL);
    return s_command_queue != NULL && s_completion_queue != NULL;
}

bool CartdeskAudioTask_Submit(const cart_audio_command_t *command)
{
    if (command == NULL || command->request_id == 0u) return false;
    if (osMessageQueuePut(s_command_queue, command, 0u, 0u) != osOK) {
        ++s_stats.queue_full;
        return false;
    }
    uint32_t depth = osMessageQueueGetCount(s_command_queue);
    if (depth > s_stats.max_queue_depth) s_stats.max_queue_depth = depth;
    return true;
}

bool CartdeskAudioTask_TryReceive(cart_audio_completion_t *completion)
{
    return completion != NULL &&
           osMessageQueueGet(s_completion_queue, completion, NULL, 0u) == osOK;
}

void CartdeskAudioTask_GetStats(cart_task_stats_t *stats)
{
    if (stats != NULL) *stats = s_stats;
}

void CartdeskAudioTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        cart_audio_command_t command;
        if (osMessageQueueGet(s_command_queue, &command, NULL, osWaitForever) != osOK) continue;
        uint32_t start_cycles = DWT->CYCCNT;

        cart_audio_completion_t completion = {
            .request_id = command.request_id,
            .owner_id = command.owner_id,
            .command = command.command,
            .state = CART_AUDIO_STATE_IDLE,
        };
        if (command.command == CART_AUDIO_CMD_STOP) completion.state = CART_AUDIO_STATE_STOPPED;
        else if (command.command == CART_AUDIO_CMD_RESET) completion.state = CART_AUDIO_STATE_RESET;

        ++s_stats.heartbeat;
        s_stats.last_heartbeat_tick = osKernelGetTickCount();
        ++s_stats.processed;
        s_stats.stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u && SystemCoreClock >= 1000000u) {
            uint32_t duration = (DWT->CYCCNT - start_cycles) / (SystemCoreClock / 1000000u);
            s_stats.last_duration_us = duration;
            if (duration > s_stats.max_duration_us) s_stats.max_duration_us = duration;
        }
        if (osMessageQueuePut(s_completion_queue, &completion, 0u, 0u) != osOK) {
            ++s_stats.queue_full;
        }
    }
}
