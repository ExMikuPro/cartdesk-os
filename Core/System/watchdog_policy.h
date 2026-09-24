#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARTDESK_WATCHDOG_NOMINAL_TIMEOUT_MS 10000u
#define CARTDESK_WATCHDOG_QFLASH_STALL_MS     5000u

void WatchdogPolicy_CaptureResetReason(void);
void WatchdogPolicy_ConfigureDebugFreeze(void);
void WatchdogPolicy_LogResetReason(void);
void WatchdogPolicy_CompleteAppIteration(uint32_t now_ms,
                                         bool qflash_exclusive,
                                         uint32_t io_heartbeat);
void WatchdogPolicy_DebugRunStallTest(void);

#ifdef __cplusplus
}
#endif
