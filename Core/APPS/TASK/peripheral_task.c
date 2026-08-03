#include "peripheral_task.h"

#include "cart_io_service.h"
#include "crash_record.h"

void CartdeskPeripheralTask_Run(void *argument)
{
    (void)argument;

    (void)CartIoService_WorkerInitialize();
    (void)CrashRecord_FlushPendingToSd();
    CartIoService_WorkerRun();
}
