#ifndef CARTDESK_CRASH_RECORD_H
#define CARTDESK_CRASH_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRASH_RECORD_MAGIC   UINT32_C(0x48464352)
#define CRASH_RECORD_VERSION UINT32_C(1)

typedef enum {
  CRASH_FAULT_HARD = 1,
  CRASH_FAULT_MEMMANAGE = 2,
  CRASH_FAULT_BUS = 3,
  CRASH_FAULT_USAGE = 4
} CrashFaultType;

typedef enum {
  CRASH_BKP_MAGIC = 0,
  CRASH_BKP_VERSION = 1,
  CRASH_BKP_FAULT_TYPE = 2,
  CRASH_BKP_SEQUENCE = 3,
  CRASH_BKP_EXC_RETURN = 4,
  CRASH_BKP_MSP = 5,
  CRASH_BKP_PSP = 6,
  CRASH_BKP_CONTROL = 7,
  CRASH_BKP_PRIMASK = 8,
  CRASH_BKP_BASEPRI = 9,
  CRASH_BKP_FAULTMASK = 10,
  CRASH_BKP_R0 = 11,
  CRASH_BKP_R1 = 12,
  CRASH_BKP_R2 = 13,
  CRASH_BKP_R3 = 14,
  CRASH_BKP_R12 = 15,
  CRASH_BKP_LR = 16,
  CRASH_BKP_PC = 17,
  CRASH_BKP_XPSR = 18,
  CRASH_BKP_CFSR = 19,
  CRASH_BKP_HFSR = 20,
  CRASH_BKP_DFSR = 21,
  CRASH_BKP_AFSR = 22,
  CRASH_BKP_MMFAR = 23,
  CRASH_BKP_BFAR = 24,
  CRASH_BKP_ICSR = 25,
  CRASH_BKP_SHCSR = 26,
  CRASH_BKP_CHECKSUM = 27,
  CRASH_BKP_WORD_COUNT = 28
} CrashBackupIndex;

typedef struct {
  uint32_t version;
  uint32_t fault_type;
  uint32_t sequence;
  uint32_t exc_return;
  uint32_t msp;
  uint32_t psp;
  uint32_t control;
  uint32_t primask;
  uint32_t basepri;
  uint32_t faultmask;
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t mmfar;
  uint32_t bfar;
  uint32_t icsr;
  uint32_t shcsr;
  uint32_t checksum;
} CrashRecord;

typedef struct {
  uint32_t msp;
  uint32_t psp;
  uint32_t control;
  uint32_t primask;
  uint32_t basepri;
  uint32_t faultmask;
} CrashExceptionState;

void CrashRecord_Init(void);
bool CrashRecord_HasPending(void);
bool CrashRecord_Read(CrashRecord *record);
bool CrashRecord_FlushPendingToSd(void);
void CrashRecord_Clear(void);
const char *CrashRecord_FaultTypeName(uint32_t type);
void CrashRecord_Print(const CrashRecord *record);

__attribute__((noreturn, used))
void CrashRecord_CaptureFromException(const uint32_t *exception_stack,
                                      uint32_t exc_return,
                                      uint32_t fault_type,
                                      const CrashExceptionState *state);

/* Executes UDF and therefore deliberately faults and resets the MCU. */
__attribute__((noreturn)) void CrashRecord_TriggerTestFault(void);

/* With CARTDESK_CRASH_TEST_ENABLE, triggers UDF once per backup-domain lifetime. */
void CrashRecord_MaybeTriggerTestFault(void);

#ifdef __cplusplus
}
#endif

#endif
