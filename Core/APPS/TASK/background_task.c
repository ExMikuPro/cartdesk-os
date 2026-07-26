#include "background_task.h"

#include "cmsis_os2.h"

#define BACKGROUND_TASK_EVENT_MASK 0x00000001u

void CartdeskBackgroundTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        (void)osThreadFlagsWait(BACKGROUND_TASK_EVENT_MASK,
                                osFlagsWaitAny,
                                osWaitForever);
        /* Reserved: run deferred work only when explicitly requested. */
    }
}
