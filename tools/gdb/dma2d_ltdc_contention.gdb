# Debug-only DMA2D bandwidth comparison with normal LTDC scanout and LTDC disabled.
set pagination off
set confirm off
file build/SizeDebug-DMA2D-SelfTest/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
tbreak Launcher_Task
continue

# R2M with normal scanout.
set variable g_dma2d_test_command = 1
finish
printf "CONTENTION mode=R2M ltdc=on cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail

tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 2
finish
printf "CONTENTION mode=M2M ltdc=on cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail

tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 4
finish
printf "CONTENTION mode=PFC ltdc=on cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail

# Stop only the LTDC scan engine; clocks, SDRAM, DMA2D, and test buffers remain unchanged.
set $ltdc_gcr = *(unsigned int*)0x50001018
set *(unsigned int*)0x50001018 = $ltdc_gcr & ~1
tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 1
finish
printf "CONTENTION mode=R2M ltdc=off cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail

tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 2
finish
printf "CONTENTION mode=M2M ltdc=off cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail

tbreak Launcher_Task
continue
set variable g_dma2d_test_command = 4
finish
printf "CONTENTION mode=PFC ltdc=off cycles=%lu us=%lu state=%lu fail=%lu\n", g_dma2d_test_cycles, g_dma2d_test_time_us, g_dma2d_test_state, g_dma2d_test_fail
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C, *(unsigned long*)0xE000ED34, *(unsigned long*)0xE000ED38
detach
quit
