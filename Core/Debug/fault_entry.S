.syntax unified
.cpu cortex-m7
.fpu fpv5-d16
.thumb

.extern g_crash_exception_state
.extern CrashRecord_CaptureFromException

.macro CRASH_FAULT_ENTRY name, fault_type
  .section .text.\name,"ax",%progbits
  .align 2
  .global \name
  .type \name, %function
  .thumb_func
\name:
  mrs     r3, primask
  cpsid   i
  mov     r1, lr
  tst     r1, #4
  ite     eq
  mrseq   r0, msp
  mrsne   r0, psp

  ldr     r12, =g_crash_exception_state
  mrs     r2, msp
  str     r2, [r12, #0]
  mrs     r2, psp
  str     r2, [r12, #4]
  mrs     r2, control
  str     r2, [r12, #8]
  str     r3, [r12, #12]
  mrs     r2, basepri
  str     r2, [r12, #16]
  mrs     r2, faultmask
  str     r2, [r12, #20]

  movs    r2, #\fault_type
  mov     r3, r12
  b       CrashRecord_CaptureFromException
  .size \name, .-\name
.endm

CRASH_FAULT_ENTRY HardFault_Handler, 1
CRASH_FAULT_ENTRY MemManage_Handler, 2
CRASH_FAULT_ENTRY BusFault_Handler, 3
CRASH_FAULT_ENTRY UsageFault_Handler, 4

.section .note.GNU-stack,"",%progbits
