# Launcher Motion Isolation Test (Debug-only).
#
# Runs one deterministic synthetic drag and dumps the motion trace ring plus
# the fault / reload counters, so CURRENT_MOTION (motion algorithms on) and
# DIRECT_DRAG_ONLY (pointer 1:1, stop on release) can be compared on the same
# pointer path.
#
# The synthetic pointer is injected inside the Launcher (see
# g_launcher_synth_touch_*), because the GT911 driver gates on a real touch.
#
#   $motion_mode 0 = CURRENT_MOTION (velocity + inertia + snap)
#   $motion_mode 1 = DIRECT_DRAG_ONLY
#   $x_step       pointer px per sample (negative = finger moves left)
#   $press_samples how many samples the finger stays down
set pagination off
set confirm off
set print elements 8192
set breakpoint pending on
init-if-undefined $motion_mode = 0
init-if-undefined $x_step = -2
init-if-undefined $press_samples = 5

file build/Debug-LTDC-Full-Render-Audit/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset init
monitor halt
load
monitor reset halt
tbreak LauncherScrollCapture_IconsReady
continue

# Keep production visuals unchanged for this test.
set variable g_render_audit_preclear_mode = 3
set variable g_launcher_rounded_fill_mode = 0
set variable g_launcher_slot_trace_visual_mode = 0
set variable g_launcher_scroll_mode = 1

# Arm the synthetic drag before the mode mailbox, so the first tick after the
# mode settles already carries synthetic input.
set variable g_launcher_synth_touch_start_x = 700
set variable g_launcher_synth_touch_step_x = $x_step
set variable g_launcher_synth_touch_press_samples = $press_samples
set variable g_launcher_synth_touch_enable = 0
set variable g_launcher_motion_mode = $motion_mode

monitor resume
shell sleep 1
monitor halt

printf "MOTION requested=%lu applied=%lu synth_enable=%lu\n", g_launcher_motion_mode, g_launcher_motion_mode_applied, g_launcher_synth_touch_enable

set $logical_before = g_launcher_logical_scroll_x
set $frames_before = g_display_frame_seq
set $presents_before = g_display_presented_frame_seq
set $samples_before = g_launcher_direct_drag_samples
set $updates_before = g_launcher_direct_drag_updates

# Start the drag and let it run: press phase plus a settle window for inertia.
set variable g_launcher_synth_touch_enable = 1
monitor resume
shell sleep 5
monitor halt

printf "RUN mode=%lu completed=%lu synth_index=%lu\n", g_launcher_motion_mode_applied, g_launcher_synth_touch_completed, g_launcher_synth_touch_enable
printf "RESULT logical_before=%ld logical_after=%ld\n", $logical_before, g_launcher_logical_scroll_x
printf "COUNTS samples_delta=%lu updates_delta=%lu frames_delta=%lu presents_delta=%lu\n", g_launcher_direct_drag_samples - $samples_before, g_launcher_direct_drag_updates - $updates_before, g_display_frame_seq - $frames_before, g_display_presented_frame_seq - $presents_before
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C, *(unsigned long*)0xE000ED34, *(unsigned long*)0xE000ED38
printf "DISPLAY render_count=%lu reload_req=%lu reload_evt=%lu reload_done=%lu timeout=%lu ownership=%lu ltdc_fu=%lu ltdc_te=%lu ltdc_other=%lu\n", g_display_render_count, g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, g_display_reload_wait_timeouts, g_display_potential_ownership_violations, g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, g_display_ltdc_other_errors

set $n = g_launcher_motion_trace_count
if $n > 256
  set $n = 256
end
set $start = g_launcher_motion_trace_index
if $n < 256
  set $start = 0
end
printf "TRACE_COUNT %lu START %lu\n", $n, $start
set $j = 0
while $j < $n
  set $idx = ($start + $j) % 256
  printf "TRACE i=%lu t=%u rs=%u px=%d pdx=%d lx=%u ldx=%d dt=%u p=%u st=%u\n", $j, g_launcher_motion_trace[$idx].time_ms, g_launcher_motion_trace[$idx].render_seq, g_launcher_motion_trace[$idx].pointer_x, g_launcher_motion_trace[$idx].pointer_dx, g_launcher_motion_trace[$idx].logical_x, g_launcher_motion_trace[$idx].logical_dx, g_launcher_motion_trace[$idx].dt_ms, g_launcher_motion_trace[$idx].pressed, g_launcher_motion_trace[$idx].state
  set $j = $j + 1
end

detach
quit
