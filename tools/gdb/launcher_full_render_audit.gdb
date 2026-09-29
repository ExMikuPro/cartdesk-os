# Automated target-side Launcher render audit. Start OpenOCD first.
set pagination off
set confirm off
set print pretty on
set print elements 512
set breakpoint pending on
init-if-undefined $preclear_mode = 0
init-if-undefined $rounded_mode = 0
file build/Debug-LTDC-Full-Render-Audit/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
tbreak LauncherScrollCapture_IconsReady
continue
set variable g_render_audit_preclear_mode = $preclear_mode
set variable g_launcher_rounded_fill_mode = $rounded_mode

# Apply normal ARGB mode and allow its invalidation to settle before arming.
set variable g_launcher_slot_trace_visual_mode = 0
tbreak DisplayTrace_RenderEnd
continue
monitor resume
shell sleep 1
monitor halt

set variable g_scroll_capture_delta_px = 240
set variable g_scroll_capture_steps = 12
set variable g_scroll_capture_step_interval_ms = 0
set variable g_fb_capture_request_step = 0xffffffff
set variable g_fb_capture_ready = 0
set variable g_display_trace_command = 1
set variable g_scroll_capture_command = 1
monitor resume
shell sleep 4
monitor halt

printf "AUDIT_STATUS scroll_state=%lu step=%lu trace_frames=%lu audit_frames=%lu clock=%lu\n", g_scroll_capture_state, g_scroll_capture_step, g_display_render_count, g_render_audit_frame_count, SystemCoreClock
printf "ROUNDED_MODE requested=%lu applied=%lu object=%p\n", g_launcher_rounded_fill_mode, g_launcher_rounded_fill_applied_mode, g_launcher_rounded_fill_object_ptr
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C, *(unsigned long*)0xE000ED34, *(unsigned long*)0xE000ED38
printf "DISPLAY render_total=%lu render_count=%lu reload_req=%lu reload_evt=%lu reload_done=%lu timeout=%lu ownership=%lu ltdc_fu=%lu ltdc_te=%lu ltdc_other=%lu\n", g_display_render_total_cycles, g_display_render_count, g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, g_display_reload_wait_timeouts, g_display_potential_ownership_violations, g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, g_display_ltdc_other_errors
printf "MERGE calls=%lu cycles=%lu comparisons=%lu invalid_before=%lu invalid_after=%lu\n", g_render_audit_merge_calls, g_render_audit_merge_cycles, g_render_audit_merge_comparisons, g_render_audit_invalid_before, g_render_audit_invalid_after
printf "PREVIEW_ALPHA seen_mask=0x%08lx normalized_pixels=%lu\n", g_launcher_preview_alpha_seen_mask, g_launcher_slot_trace_alpha_fixed_pixels
p g_launcher_preview_nonopaque_pixels
p g_launcher_preview_min_alpha
p g_render_audit_total
p g_render_audit_last
p g_render_audit_rects
p g_render_audit_preclear_rects
set $preclear_index = 0
while $preclear_index < g_render_audit_last.preclear_area_count
  printf "PRECLEAR_RECT index=%lu x1=%d y1=%d x2=%d y2=%d cleared=%u opaque_cover=%u\n", $preclear_index, g_render_audit_preclear_rects[$preclear_index].x1, g_render_audit_preclear_rects[$preclear_index].y1, g_render_audit_preclear_rects[$preclear_index].x2, g_render_audit_preclear_rects[$preclear_index].y2, g_render_audit_preclear_rects[$preclear_index].cleared, g_render_audit_preclear_rects[$preclear_index].opaque_cover
  set $preclear_index = $preclear_index + 1
end
set $task_index = 0
while $task_index < g_render_audit_last.rect_count
  printf "DRAW_RECT index=%lu x1=%d y1=%d x2=%d y2=%d type=%u coverage=%u unit=%u cycles=%lu\n", $task_index, g_render_audit_rects[$task_index].x1, g_render_audit_rects[$task_index].y1, g_render_audit_rects[$task_index].x2, g_render_audit_rects[$task_index].y2, g_render_audit_rects[$task_index].type, g_render_audit_rects[$task_index].reserved[0], g_render_audit_rects[$task_index].unit, g_render_audit_rects[$task_index].exec_cycles
  set $task_index = $task_index + 1
end
detach
quit
