#ifndef CARTDESK_PERIPHERAL_TASK_H
#define CARTDESK_PERIPHERAL_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reserved for serialized blocking peripheral operations. Fast GPIO register
 * updates and ISR-only acknowledgement do not need to be routed through it.
 */
void CartdeskPeripheralTask_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif
