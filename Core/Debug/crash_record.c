#include "crash_record.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cart_log.h"
#include "fatfs.h"
#include "stm32h7xx.h"

#ifndef CARTDESK_CRASH_BREAK_WHEN_DEBUGGED
#define CARTDESK_CRASH_BREAK_WHEN_DEBUGGED 0
#endif

#ifndef CARTDESK_CRASH_TEST_ENABLE
#define CARTDESK_CRASH_TEST_ENABLE 0
#endif

#define CRASH_TEST_MARKER_INDEX 28U
#define CRASH_TEST_MARKER       UINT32_C(0x43525453)
#define CRASH_FNV_OFFSET        UINT32_C(2166136261)
#define CRASH_FNV_PRIME         UINT32_C(16777619)
#define CRASH_LOG_CAPACITY      3072U
/* Architectural CFSR UsageFault STKOF bit; this CMSIS-M7 header omits a name. */
#define CRASH_CFSR_STKOF_Msk    UINT32_C(0x00100000)

CrashExceptionState g_crash_exception_state;

static volatile uint32_t g_capture_active;
static volatile uint32_t g_capture_words[CRASH_BKP_WORD_COUNT];
static CrashRecord g_pending_record;
static bool g_has_pending;
static char g_log_buffer[CRASH_LOG_CAPACITY];
static FIL g_crash_file;

static volatile uint32_t *CrashRecord_BackupRegisters(void)
{
  return &RTC->BKP0R;
}

static void CrashRecord_EnableBackupWrite(void)
{
  RCC->APB4ENR |= RCC_APB4ENR_RTCAPBEN;
  (void)RCC->APB4ENR;
  PWR->CR1 |= PWR_CR1_DBP;
  for (volatile uint32_t timeout = 0; timeout < 100000U; ++timeout) {
    if ((PWR->CR1 & PWR_CR1_DBP) != 0U) {
      break;
    }
  }
}

static uint32_t CrashRecord_ChecksumWords(const volatile uint32_t *words)
{
  uint32_t hash = CRASH_FNV_OFFSET;
  for (uint32_t index = CRASH_BKP_VERSION; index < CRASH_BKP_CHECKSUM; ++index) {
    uint32_t word = words[index];
    for (uint32_t byte = 0; byte < 4U; ++byte) {
      hash ^= (word >> (byte * 8U)) & 0xFFU;
      hash *= CRASH_FNV_PRIME;
    }
  }
  return hash;
}

static uint32_t CrashRecord_ChecksumBackup(volatile uint32_t *bkp)
{
  uint32_t hash = CRASH_FNV_OFFSET;
  for (uint32_t index = CRASH_BKP_VERSION; index < CRASH_BKP_CHECKSUM; ++index) {
    uint32_t word = bkp[index];
    for (uint32_t byte = 0; byte < 4U; ++byte) {
      hash ^= (word >> (byte * 8U)) & 0xFFU;
      hash *= CRASH_FNV_PRIME;
    }
  }
  return hash;
}

static bool CrashRecord_FaultTypeValid(uint32_t type)
{
  return type >= CRASH_FAULT_HARD && type <= CRASH_FAULT_USAGE;
}

static bool CrashRecord_ReadWords(uint32_t words[CRASH_BKP_WORD_COUNT])
{
  volatile uint32_t *bkp = CrashRecord_BackupRegisters();
  if (bkp[CRASH_BKP_MAGIC] != CRASH_RECORD_MAGIC) {
    return false;
  }

  for (uint32_t i = 0; i < CRASH_BKP_WORD_COUNT; ++i) {
    words[i] = bkp[i];
  }

  return words[CRASH_BKP_MAGIC] == CRASH_RECORD_MAGIC &&
         words[CRASH_BKP_VERSION] == CRASH_RECORD_VERSION &&
         CrashRecord_FaultTypeValid(words[CRASH_BKP_FAULT_TYPE]) &&
         words[CRASH_BKP_CHECKSUM] == CrashRecord_ChecksumWords(words);
}

static void CrashRecord_Deserialize(const uint32_t *w, CrashRecord *r)
{
  r->version = w[CRASH_BKP_VERSION];
  r->fault_type = w[CRASH_BKP_FAULT_TYPE];
  r->sequence = w[CRASH_BKP_SEQUENCE];
  r->exc_return = w[CRASH_BKP_EXC_RETURN];
  r->msp = w[CRASH_BKP_MSP];
  r->psp = w[CRASH_BKP_PSP];
  r->control = w[CRASH_BKP_CONTROL];
  r->primask = w[CRASH_BKP_PRIMASK];
  r->basepri = w[CRASH_BKP_BASEPRI];
  r->faultmask = w[CRASH_BKP_FAULTMASK];
  r->r0 = w[CRASH_BKP_R0];
  r->r1 = w[CRASH_BKP_R1];
  r->r2 = w[CRASH_BKP_R2];
  r->r3 = w[CRASH_BKP_R3];
  r->r12 = w[CRASH_BKP_R12];
  r->lr = w[CRASH_BKP_LR];
  r->pc = w[CRASH_BKP_PC];
  r->xpsr = w[CRASH_BKP_XPSR];
  r->cfsr = w[CRASH_BKP_CFSR];
  r->hfsr = w[CRASH_BKP_HFSR];
  r->dfsr = w[CRASH_BKP_DFSR];
  r->afsr = w[CRASH_BKP_AFSR];
  r->mmfar = w[CRASH_BKP_MMFAR];
  r->bfar = w[CRASH_BKP_BFAR];
  r->icsr = w[CRASH_BKP_ICSR];
  r->shcsr = w[CRASH_BKP_SHCSR];
  r->checksum = w[CRASH_BKP_CHECKSUM];
}

static bool CrashRecord_RangeInInternalRam(uintptr_t start, size_t size)
{
  if ((start & (sizeof(uint32_t) - 1U)) != 0U || start > UINTPTR_MAX - size) {
    return false;
  }
  uintptr_t end = start + size;
  return (start >= UINT32_C(0x20000000) && end <= UINT32_C(0x20020000)) ||
         (start >= UINT32_C(0x24000000) && end <= UINT32_C(0x24080000)) ||
         (start >= UINT32_C(0x30000000) && end <= UINT32_C(0x30048000)) ||
         (start >= UINT32_C(0x38000000) && end <= UINT32_C(0x38010000));
}

static bool CrashRecord_BasicFrameValid(uintptr_t address)
{
  if (!CrashRecord_RangeInInternalRam(address, 8U * sizeof(uint32_t))) {
    return false;
  }
  const uint32_t *frame = (const uint32_t *)address;
  return (frame[7] & xPSR_T_Msk) != 0U;
}

static size_t CrashRecord_Append(char *buffer, size_t capacity, size_t used,
                                 const char *format, ...)
{
  if (used >= capacity) {
    return capacity;
  }
  va_list args;
  va_start(args, format);
  int written = vsnprintf(buffer + used, capacity - used, format, args);
  va_end(args);
  if (written < 0) {
    return capacity;
  }
  if ((size_t)written >= capacity - used) {
    return capacity;
  }
  return used + (size_t)written;
}

typedef struct {
  uint32_t mask;
  const char *name;
} CrashReason;

static const CrashReason k_cfsr_reasons[] = {
    {SCB_CFSR_IACCVIOL_Msk, "IACCVIOL"},
    {SCB_CFSR_DACCVIOL_Msk, "DACCVIOL"},
    {SCB_CFSR_MUNSTKERR_Msk, "MUNSTKERR"},
    {SCB_CFSR_MSTKERR_Msk, "MSTKERR"},
    {SCB_CFSR_MLSPERR_Msk, "MLSPERR"},
    {SCB_CFSR_MMARVALID_Msk, "MMARVALID"},
    {SCB_CFSR_IBUSERR_Msk, "IBUSERR"},
    {SCB_CFSR_PRECISERR_Msk, "PRECISERR"},
    {SCB_CFSR_IMPRECISERR_Msk, "IMPRECISERR"},
    {SCB_CFSR_UNSTKERR_Msk, "UNSTKERR"},
    {SCB_CFSR_STKERR_Msk, "STKERR"},
    {SCB_CFSR_LSPERR_Msk, "LSPERR"},
    {SCB_CFSR_BFARVALID_Msk, "BFARVALID"},
    {SCB_CFSR_UNDEFINSTR_Msk, "UNDEFINSTR"},
    {SCB_CFSR_INVSTATE_Msk, "INVSTATE"},
    {SCB_CFSR_INVPC_Msk, "INVPC"},
    {SCB_CFSR_NOCP_Msk, "NOCP"},
    {CRASH_CFSR_STKOF_Msk, "STKOF"},
    {SCB_CFSR_UNALIGNED_Msk, "UNALIGNED"},
    {SCB_CFSR_DIVBYZERO_Msk, "DIVBYZERO"},
};

static const CrashReason k_hfsr_reasons[] = {
    {SCB_HFSR_VECTTBL_Msk, "VECTTBL"},
    {SCB_HFSR_FORCED_Msk, "FORCED"},
    {SCB_HFSR_DEBUGEVT_Msk, "DEBUGEVT"},
};

static size_t CrashRecord_AppendReasons(char *buffer, size_t capacity, size_t used,
                                        const CrashRecord *record)
{
  bool first = true;
  used = CrashRecord_Append(buffer, capacity, used, "reasons=");
  for (size_t i = 0; i < sizeof(k_cfsr_reasons) / sizeof(k_cfsr_reasons[0]); ++i) {
    if ((record->cfsr & k_cfsr_reasons[i].mask) != 0U) {
      used = CrashRecord_Append(buffer, capacity, used, "%s%s",
                                first ? "" : ",", k_cfsr_reasons[i].name);
      first = false;
    }
  }
  for (size_t i = 0; i < sizeof(k_hfsr_reasons) / sizeof(k_hfsr_reasons[0]); ++i) {
    if ((record->hfsr & k_hfsr_reasons[i].mask) != 0U) {
      used = CrashRecord_Append(buffer, capacity, used, "%s%s",
                                first ? "" : ",", k_hfsr_reasons[i].name);
      first = false;
    }
  }
  if (first) {
    used = CrashRecord_Append(buffer, capacity, used, "none");
  }
  return CrashRecord_Append(buffer, capacity, used, "\n");
}

static size_t CrashRecord_FormatLog(const CrashRecord *r)
{
  size_t n = 0;
  n = CrashRecord_Append(g_log_buffer, sizeof(g_log_buffer), n,
                         "BEGIN CRASH RECORD\n"
                         "version=%lu\nfault_type=%s\nfault_type_id=%lu\n"
                         "sequence=%lu\nexc_return=0x%08lX\n"
                         "msp=0x%08lX\npsp=0x%08lX\ncontrol=0x%08lX\n"
                         "primask=0x%08lX\nbasepri=0x%08lX\nfaultmask=0x%08lX\n"
                         "r0=0x%08lX\nr1=0x%08lX\nr2=0x%08lX\nr3=0x%08lX\n"
                         "r12=0x%08lX\nlr=0x%08lX\npc=0x%08lX\nxpsr=0x%08lX\n"
                         "cfsr=0x%08lX\nhfsr=0x%08lX\ndfsr=0x%08lX\n"
                         "afsr=0x%08lX\nmmfar=0x%08lX\nbfar=0x%08lX\n"
                         "icsr=0x%08lX\nshcsr=0x%08lX\nchecksum=0x%08lX\n",
                         (unsigned long)r->version,
                         CrashRecord_FaultTypeName(r->fault_type),
                         (unsigned long)r->fault_type,
                         (unsigned long)r->sequence,
                         (unsigned long)r->exc_return,
                         (unsigned long)r->msp, (unsigned long)r->psp,
                         (unsigned long)r->control, (unsigned long)r->primask,
                         (unsigned long)r->basepri, (unsigned long)r->faultmask,
                         (unsigned long)r->r0, (unsigned long)r->r1,
                         (unsigned long)r->r2, (unsigned long)r->r3,
                         (unsigned long)r->r12, (unsigned long)r->lr,
                         (unsigned long)r->pc, (unsigned long)r->xpsr,
                         (unsigned long)r->cfsr, (unsigned long)r->hfsr,
                         (unsigned long)r->dfsr, (unsigned long)r->afsr,
                         (unsigned long)r->mmfar, (unsigned long)r->bfar,
                         (unsigned long)r->icsr, (unsigned long)r->shcsr,
                         (unsigned long)r->checksum);
  n = CrashRecord_AppendReasons(g_log_buffer, sizeof(g_log_buffer), n, r);
  if ((r->cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) {
    n = CrashRecord_Append(g_log_buffer, sizeof(g_log_buffer), n,
                           "valid_mmfar=0x%08lX\n", (unsigned long)r->mmfar);
  }
  if ((r->cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) {
    n = CrashRecord_Append(g_log_buffer, sizeof(g_log_buffer), n,
                           "valid_bfar=0x%08lX\n", (unsigned long)r->bfar);
  }
  return CrashRecord_Append(g_log_buffer, sizeof(g_log_buffer), n,
                            "END CRASH RECORD\n");
}

void CrashRecord_Init(void)
{
  uint32_t words[CRASH_BKP_WORD_COUNT];
  g_has_pending = false;

  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk |
                SCB_SHCSR_BUSFAULTENA_Msk |
                SCB_SHCSR_USGFAULTENA_Msk;

  if (CrashRecord_ReadWords(words)) {
    CrashRecord_Deserialize(words, &g_pending_record);
    g_has_pending = true;
  } else if (CrashRecord_BackupRegisters()[CRASH_BKP_MAGIC] == CRASH_RECORD_MAGIC) {
    printf("[CRASH] invalid backup record (version/type/checksum)\r\n");
  }
}

bool CrashRecord_HasPending(void)
{
  return g_has_pending;
}

bool CrashRecord_Read(CrashRecord *record)
{
  if (!g_has_pending || record == NULL) {
    return false;
  }
  *record = g_pending_record;
  return true;
}

const char *CrashRecord_FaultTypeName(uint32_t type)
{
  switch (type) {
    case CRASH_FAULT_HARD: return "HardFault";
    case CRASH_FAULT_MEMMANAGE: return "MemManage";
    case CRASH_FAULT_BUS: return "BusFault";
    case CRASH_FAULT_USAGE: return "UsageFault";
    default: return "Unknown";
  }
}

void CrashRecord_Print(const CrashRecord *r)
{
  if (r == NULL) {
    return;
  }
  printf("[CRASH] pending fault record detected\r\n");
  printf("type=%s version=%lu sequence=%lu\r\n",
         CrashRecord_FaultTypeName(r->fault_type),
         (unsigned long)r->version, (unsigned long)r->sequence);
  printf("pc=0x%08lX lr=0x%08lX xpsr=0x%08lX\r\n",
         (unsigned long)r->pc, (unsigned long)r->lr, (unsigned long)r->xpsr);
  printf("cfsr=0x%08lX hfsr=0x%08lX\r\n",
         (unsigned long)r->cfsr, (unsigned long)r->hfsr);
  if ((r->cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) {
    printf("mmfar=0x%08lX (valid)\r\n", (unsigned long)r->mmfar);
  }
  if ((r->cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) {
    printf("bfar=0x%08lX (valid)\r\n", (unsigned long)r->bfar);
  }
  printf("msp=0x%08lX psp=0x%08lX exc_return=0x%08lX\r\n",
         (unsigned long)r->msp, (unsigned long)r->psp,
         (unsigned long)r->exc_return);
  size_t n = CrashRecord_AppendReasons(g_log_buffer, sizeof(g_log_buffer), 0, r);
  if (n < sizeof(g_log_buffer)) {
    printf("%s", g_log_buffer);
  }
}

void CrashRecord_Clear(void)
{
  CrashRecord_EnableBackupWrite();
  CrashRecord_BackupRegisters()[CRASH_BKP_MAGIC] = 0U;
  __DSB();
  __ISB();
  g_has_pending = false;
}

bool CrashRecord_FlushPendingToSd(void)
{
  if (!g_has_pending) {
    return true;
  }

  FRESULT fr = SD_FATFS_Mount();
  if (fr != FR_OK) {
    char message[96];
    (void)snprintf(message, sizeof(message), "SD mount failed: %u; record retained",
                   (unsigned)fr);
    CartLog_Write(CART_LOG_ERROR, "crash", message);
    SD_FATFS_InvalidateMount();
    return false;
  }

  fr = f_mkdir("0:/logs");
  if (fr != FR_OK && fr != FR_EXIST) {
    char message[96];
    (void)snprintf(message, sizeof(message),
                   "create logs directory failed: %u; record retained", (unsigned)fr);
    CartLog_Write(CART_LOG_ERROR, "crash", message);
    return false;
  }

  size_t length = CrashRecord_FormatLog(&g_pending_record);
  if (length >= sizeof(g_log_buffer)) {
    CartLog_Write(CART_LOG_ERROR, "crash", "log formatting overflow; record retained");
    return false;
  }

  fr = f_open(&g_crash_file, "0:/logs/crash.log", FA_WRITE | FA_OPEN_APPEND);
  if (fr != FR_OK) {
    char message[96];
    (void)snprintf(message, sizeof(message),
                   "open crash.log failed: %u; record retained", (unsigned)fr);
    CartLog_Write(CART_LOG_ERROR, "crash", message);
    return false;
  }

  UINT written = 0;
  FRESULT write_fr = f_write(&g_crash_file, g_log_buffer, (UINT)length, &written);
  FRESULT sync_fr = (write_fr == FR_OK && written == length)
                        ? f_sync(&g_crash_file)
                        : write_fr;
  FRESULT close_fr = f_close(&g_crash_file);

  if (write_fr != FR_OK || written != length || sync_fr != FR_OK ||
      close_fr != FR_OK) {
    char message[160];
    (void)snprintf(message, sizeof(message),
                   "SD write failed: write=%u bytes=%u/%lu sync=%u close=%u; retained",
                   (unsigned)write_fr, (unsigned)written, (unsigned long)length,
                   (unsigned)sync_fr, (unsigned)close_fr);
    CartLog_Write(CART_LOG_ERROR, "crash", message);
    SD_FATFS_InvalidateMount();
    return false;
  }

  CrashRecord_Clear();
  CartLog_Write(CART_LOG_INFO, "crash", "record appended to 0:/logs/crash.log");
  return true;
}

__attribute__((noreturn, used))
void CrashRecord_CaptureFromException(const uint32_t *exception_stack,
                                      uint32_t exc_return,
                                      uint32_t fault_type,
                                      const CrashExceptionState *state)
{
  __disable_irq();
  if (g_capture_active != 0U) {
    NVIC_SystemReset();
    for (;;) {
    }
  }
  g_capture_active = 1U;

  CrashRecord_EnableBackupWrite();
  volatile uint32_t *bkp = CrashRecord_BackupRegisters();
  uint32_t previous_sequence = 0U;
  if (bkp[CRASH_BKP_VERSION] == CRASH_RECORD_VERSION &&
      CrashRecord_FaultTypeValid(bkp[CRASH_BKP_FAULT_TYPE]) &&
      bkp[CRASH_BKP_CHECKSUM] == CrashRecord_ChecksumBackup(bkp)) {
    previous_sequence = bkp[CRASH_BKP_SEQUENCE];
  }

  volatile uint32_t *words = g_capture_words;
  for (uint32_t i = 0; i < CRASH_BKP_WORD_COUNT; ++i) {
    words[i] = 0U;
  }

  words[CRASH_BKP_VERSION] = CRASH_RECORD_VERSION;
  words[CRASH_BKP_FAULT_TYPE] = fault_type;
  words[CRASH_BKP_SEQUENCE] = previous_sequence + 1U;
  words[CRASH_BKP_EXC_RETURN] = exc_return;
  words[CRASH_BKP_MSP] = state->msp;
  words[CRASH_BKP_PSP] = state->psp;
  words[CRASH_BKP_CONTROL] = state->control;
  words[CRASH_BKP_PRIMASK] = state->primask;
  words[CRASH_BKP_BASEPRI] = state->basepri;
  words[CRASH_BKP_FAULTMASK] = state->faultmask;

  uintptr_t basic_frame = (uintptr_t)exception_stack;
  if ((exc_return & (1UL << 4)) == 0U) {
    uintptr_t extended_basic = basic_frame + 18U * sizeof(uint32_t);
    /*
     * With lazy FP preservation, Cortex-M7 can report an extended EXC_RETURN
     * while the basic frame is still at the raw SP. Prefer the architectural
     * extended-frame offset, but fall back only when its xPSR is not valid.
     */
    if (CrashRecord_BasicFrameValid(extended_basic)) {
      basic_frame = extended_basic;
    }
  }
  if (CrashRecord_BasicFrameValid(basic_frame)) {
    const uint32_t *stacked = (const uint32_t *)basic_frame;
    words[CRASH_BKP_R0] = stacked[0];
    words[CRASH_BKP_R1] = stacked[1];
    words[CRASH_BKP_R2] = stacked[2];
    words[CRASH_BKP_R3] = stacked[3];
    words[CRASH_BKP_R12] = stacked[4];
    words[CRASH_BKP_LR] = stacked[5];
    words[CRASH_BKP_PC] = stacked[6];
    words[CRASH_BKP_XPSR] = stacked[7];
  }

  words[CRASH_BKP_CFSR] = SCB->CFSR;
  words[CRASH_BKP_HFSR] = SCB->HFSR;
  words[CRASH_BKP_DFSR] = SCB->DFSR;
  words[CRASH_BKP_AFSR] = SCB->AFSR;
  words[CRASH_BKP_MMFAR] = SCB->MMFAR;
  words[CRASH_BKP_BFAR] = SCB->BFAR;
  words[CRASH_BKP_ICSR] = SCB->ICSR;
  words[CRASH_BKP_SHCSR] = SCB->SHCSR;
  words[CRASH_BKP_CHECKSUM] = CrashRecord_ChecksumWords(words);

  bkp[CRASH_BKP_MAGIC] = 0U;
  for (uint32_t i = CRASH_BKP_VERSION; i <= CRASH_BKP_CHECKSUM; ++i) {
    bkp[i] = words[i];
  }
  __DSB();
  bkp[CRASH_BKP_MAGIC] = CRASH_RECORD_MAGIC;
  __DSB();
  __ISB();

#if CARTDESK_CRASH_BREAK_WHEN_DEBUGGED
  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
    __BKPT(0);
  }
#endif

  NVIC_SystemReset();
  for (;;) {
  }
}

__attribute__((noreturn)) void CrashRecord_TriggerTestFault(void)
{
  __asm volatile("udf #0");
  for (;;) {
  }
}

void CrashRecord_MaybeTriggerTestFault(void)
{
#if CARTDESK_CRASH_TEST_ENABLE
  if (g_has_pending) {
    return;
  }
  CrashRecord_EnableBackupWrite();
  volatile uint32_t *bkp = CrashRecord_BackupRegisters();
  if (bkp[CRASH_TEST_MARKER_INDEX] != CRASH_TEST_MARKER) {
    bkp[CRASH_TEST_MARKER_INDEX] = CRASH_TEST_MARKER;
    __DSB();
    __ISB();
    CrashRecord_TriggerTestFault();
  }
#endif
}
