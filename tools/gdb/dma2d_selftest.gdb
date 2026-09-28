# Run OpenOCD separately, for example: openocd -f CartDeck.cfg
# Then from the repository root: arm-none-eabi-gdb -x tools/gdb/dma2d_selftest.gdb

set pagination off
set confirm off
set print pretty on
file build/SizeDebug-DMA2D-SelfTest/cartdesk-os.elf
target extended-remote localhost:3333

set $dma2d_selftest_fault = 0
break CrashRecord_CaptureFromException
commands
  silent
  set $dma2d_selftest_fault = 1
  printf "\nFault capture: pc=0x%08lx lr=0x%08lx sp=0x%08lx cfsr=0x%08lx hfsr=0x%08lx mmfar=0x%08lx bfar=0x%08lx\n", *((unsigned int *)($r0 + 24)), *((unsigned int *)($r0 + 20)), *((unsigned int *)$r3), SCB->CFSR, SCB->HFSR, SCB->MMFAR, SCB->BFAR
  continue
end

load
monitor reset halt
tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 4
finish

printf "\nDMA2D self-test: state=%lu stage=%lu pass=%lu fail=%lu operation=%lu case=%lu fault=%lu\n", g_dma2d_test_state, g_dma2d_test_stage, g_dma2d_test_pass, g_dma2d_test_fail, g_dma2d_test_operation, g_dma2d_test_case, $dma2d_selftest_fault
printf "first error: x=%lu y=%lu expected=0x%08lx actual=0x%08lx\n", g_dma2d_test_error_x, g_dma2d_test_error_y, g_dma2d_test_expected, g_dma2d_test_actual
printf "LOM=%lu width=%lu height=%lu source=0x%08lx source_stride=%lu destination=0x%08lx destination_stride=%lu cycles=%lu time_us=%lu\n", g_dma2d_test_lom, g_dma2d_test_width, g_dma2d_test_height, g_dma2d_test_source_address, g_dma2d_test_source_stride, g_dma2d_test_destination_address, g_dma2d_test_destination_stride, g_dma2d_test_cycles, g_dma2d_test_time_us
printf "CR=0x%08lx ISR=0x%08lx FGMAR=0x%08lx FGOR=%lu FGPFCCR=0x%08lx BGMAR=0x%08lx BGOR=%lu BGPFCCR=0x%08lx OMAR=0x%08lx OOR=%lu OPFCCR=0x%08lx NLR=0x%08lx\n", g_dma2d_test_registers.cr, g_dma2d_test_registers.isr, g_dma2d_test_registers.fgmar, g_dma2d_test_registers.fgor, g_dma2d_test_registers.fgpfccr, g_dma2d_test_registers.bgmar, g_dma2d_test_registers.bgor, g_dma2d_test_registers.bgpfccr, g_dma2d_test_registers.omar, g_dma2d_test_registers.oor, g_dma2d_test_registers.opfccr, g_dma2d_test_registers.nlr
detach
quit
