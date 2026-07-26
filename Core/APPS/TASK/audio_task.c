#include "audio_task.h"

#include "cmsis_os2.h"

#define AUDIO_TASK_EVENT_MASK 0x00000001u

void CartdeskAudioTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        (void)osThreadFlagsWait(AUDIO_TASK_EVENT_MASK,
                                osFlagsWaitAny,
                                osWaitForever);
        /* Reserved: submit/refill DMA audio buffers outside the ISR. */
    }
}
