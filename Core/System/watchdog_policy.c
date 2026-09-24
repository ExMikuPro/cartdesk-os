#include "watchdog_policy.h"

#include "cart_log.h"
#include "iwdg.h"
#include "stm32h7xx_hal.h"

typedef struct {
  uint32_t qflash_progress_tick;
  uint32_t qflash_heartbeat;
  bool qflash_active;
  bool reset_was_iwdg;
} WatchdogPolicyState;

static WatchdogPolicyState s_watchdog;

void WatchdogPolicy_CaptureResetReason(void) {
  s_watchdog.reset_was_iwdg =
      __HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0u;
  __HAL_RCC_CLEAR_RESET_FLAGS();
}

void WatchdogPolicy_ConfigureDebugFreeze(void) {
#if CARTDESK_WATCHDOG_DEBUG_FREEZE
  __HAL_DBGMCU_FREEZE_IWDG1();
#endif
}

void WatchdogPolicy_LogResetReason(void) {
  if (s_watchdog.reset_was_iwdg) {
    CartLog_Write(CART_LOG_WARN, "watchdog", "reset_reason=IWDG");
  }
}

void WatchdogPolicy_CompleteAppIteration(uint32_t now_ms,
                                         bool qflash_exclusive,
                                         uint32_t io_heartbeat) {
  bool healthy = true;

  if (qflash_exclusive) {
    if (!s_watchdog.qflash_active ||
        io_heartbeat != s_watchdog.qflash_heartbeat) {
      s_watchdog.qflash_active = true;
      s_watchdog.qflash_progress_tick = now_ms;
      s_watchdog.qflash_heartbeat = io_heartbeat;
    } else if ((uint32_t)(now_ms - s_watchdog.qflash_progress_tick) >
               CARTDESK_WATCHDOG_QFLASH_STALL_MS) {
      healthy = false;
    }
  } else {
    s_watchdog.qflash_active = false;
    s_watchdog.qflash_progress_tick = now_ms;
    s_watchdog.qflash_heartbeat = io_heartbeat;
  }

  if (healthy) {
    (void)HAL_IWDG_Refresh(&hiwdg1);
  }
}

void WatchdogPolicy_DebugRunStallTest(void) {
#if CARTDESK_WATCHDOG_STALL_TEST_ENABLE
  __HAL_DBGMCU_UnFreeze_IWDG1();
  for (;;) {
    __NOP();
  }
#endif
}
