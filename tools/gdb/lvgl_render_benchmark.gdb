# Run with: arm-none-eabi-gdb --batch -ex 'set $scene=1' -x tools/gdb/lvgl_render_benchmark.gdb
# Scene 1..6 selects L0, L1, L2, L3 XRGB, L4 ARGB, or L5 alpha<255.
set pagination off
set confirm off
set print pretty on
set print elements 128
init-if-undefined $preclear_mode = 0
file build/Debug-LTDC-Full-Render-Audit/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
tbreak LauncherScrollCapture_IconsReady
continue
set variable g_render_audit_preclear_mode = $preclear_mode

set variable g_lvgl_render_bench_command = $scene
monitor resume
shell sleep 1
monitor halt
printf "BENCH_READY scene=%lu state=%lu\n", g_lvgl_render_bench_scene, g_lvgl_render_bench_state

set variable g_display_trace_command = 1
monitor resume
shell sleep 1
monitor halt
set variable g_lvgl_render_bench_run = 1
monitor resume
shell sleep 7
monitor halt

printf "BENCH_RESULT scene=%lu state=%lu steps=%lu frames=%lu clock=%lu\n", g_lvgl_render_bench_scene, g_lvgl_render_bench_state, g_lvgl_render_bench_step, g_display_render_count, SystemCoreClock
printf "DISPLAY render_total=%lu render_min=%lu render_max=%lu flush_pixels=%lu largest_flush=%lu flushes=%lu\n", g_display_render_total_cycles, g_display_render_min_cycles, g_display_render_max_cycles, g_display_flush_pixels, g_display_largest_flush_pixels, g_display_flush_count
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C, *(unsigned long*)0xE000ED34, *(unsigned long*)0xE000ED38
p g_render_audit_total
detach
quit
