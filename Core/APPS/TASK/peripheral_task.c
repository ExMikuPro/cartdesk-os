#include "peripheral_task.h"

#include "cmsis_os2.h"

#define PERIPHERAL_TASK_EVENT_MASK 0x00000001u

void CartdeskPeripheralTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        (void)osThreadFlagsWait(PERIPHERAL_TASK_EVENT_MASK,
                                osFlagsWaitAny,
                                osWaitForever);
        /* Reserved: process bounded GPIO/I2C/SPI/SD requests from a queue. */
    }
}
