# Phase 5 A/B: Launcher native scroll versus slot-local redraw.
#
# Start OpenOCD first (openocd -f CartDeck.cfg), then run one mode per session:
#
#   arm-none-eabi-gdb -q -batch \
#     -ex 'set $scroll_mode = 0' -ex 'set $move_api = 0' \
#     -x tools/gdb/launcher_slot_local_redraw.gdb | tee /tmp/mode0.log
#
#   arm-none-eabi-gdb -q -batch \
#     -ex 'set $scroll_mode = 1' -ex 'set $move_api = 0' \
#     -x tools/gdb/launcher_slot_local_redraw.gdb | tee /tmp/mode1.log
#
# $scroll_mode 0 = production native LVGL scroll container
# $scroll_mode 1 = slot-local movement driven by Launcher logical_scroll_x
# $move_api    0 = lv_obj_set_x, 1 = lv_obj_set_style_translate_x (candidate only)
#
# The 12-step deterministic harness is +240 px in 20 px steps, identical to the
# Phase 3 / Phase 4 / Phase 5 rounded-fill benchmarks so T_render is comparable.
set pagination off
set confirm off
set print elements 4096
set breakpoint pending on
init-if-undefined $scroll_mode = 0
init-if-undefined $move_api = 0
init-if-undefined $delta = 240
init-if-undefined $steps = 12
# Optional unmeasured forward pre-roll so the measured phase can test reversal
# (for example $preroll = 240 with $delta = -240).
init-if-undefined $preroll = 0

file build/Debug-LTDC-Full-Render-Audit/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
tbreak LauncherScrollCapture_IconsReady
continue

# Keep the retained Phase 3 DMA2D pre-clear active and normal ARGB previews.
set variable g_render_audit_preclear_mode = 3
set variable g_launcher_rounded_fill_mode = 0
set variable g_launcher_slot_trace_visual_mode = 0
set variable g_launcher_slot_move_api = $move_api
set variable g_launcher_scroll_mode = $scroll_mode

# Let Launcher_Task observe the mailbox and settle the mode switch.
monitor resume
shell sleep 1
monitor halt

printf "MODE requested=%lu applied=%lu move_api=%lu logical_scroll_x=%ld scroll_max=%ld\n", g_launcher_scroll_mode, g_launcher_scroll_mode_applied, g_launcher_slot_move_api, g_launcher_logical_scroll_x, g_launcher_logical_scroll_max
printf "MODE native_scroll_x=%ld scroll_dir_hor=%u\n", (g_launcher_scroll_mode == 1 ? -1 : 0), (g_launcher_scroll_mode == 1 ? 0 : 1)

if $preroll != 0
  set variable g_scroll_capture_delta_px = $preroll
  set variable g_scroll_capture_steps = 12
  set variable g_scroll_capture_step_interval_ms = 0
  set variable g_fb_capture_request_step = 0xffffffff
  set variable g_fb_capture_ready = 0
  set variable g_scroll_capture_command = 1
  monitor resume
  shell sleep 4
  monitor halt
  printf "PREROLL delta=%ld state=%lu step=%lu scroll_x=%ld\n", g_scroll_capture_delta_px, g_scroll_capture_state, g_scroll_capture_step, g_launcher_logical_scroll_x
end

# Enabling the trace resets the render audit, so the dirty-ring starts clean.
set variable g_scroll_capture_command = 0
set variable g_scroll_capture_delta_px = $delta
set variable g_scroll_capture_steps = $steps
set variable g_scroll_capture_step_interval_ms = 0
set variable g_fb_capture_request_step = 0xffffffff
set variable g_fb_capture_ready = 0
set variable g_display_trace_command = 1
set variable g_scroll_capture_command = 1
monitor resume
# GDB does not expand convenience variables inside `shell`, so the measured
# window keeps a literal duration.
shell sleep 4
monitor halt

printf "SWEEP delta=%ld steps=%lu\n", $delta, $steps
printf "AUDIT_STATUS scroll_state=%lu step=%lu trace_frames=%lu audit_frames=%lu clock=%lu\n", g_scroll_capture_state, g_scroll_capture_step, g_display_render_count, g_render_audit_frame_count, SystemCoreClock
printf "SCROLL_MODE applied=%lu move_api=%lu logical_scroll_x=%ld slot_moves=%lu visible_mask=0x%03lx\n", g_launcher_scroll_mode_applied, g_launcher_slot_move_api, g_launcher_logical_scroll_x, g_launcher_slot_moves, g_launcher_slot_visible_mask
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C, *(unsigned long*)0xE000ED34, *(unsigned long*)0xE000ED38
printf "DISPLAY render_total=%lu render_count=%lu reload_req=%lu reload_evt=%lu reload_done=%lu timeout=%lu ownership=%lu ltdc_fu=%lu ltdc_te=%lu ltdc_other=%lu\n", g_display_render_total_cycles, g_display_render_count, g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, g_display_reload_wait_timeouts, g_display_potential_ownership_violations, g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, g_display_ltdc_other_errors
printf "TOTAL frames=%lu cycles=%lu\n", g_render_audit_frame_count, g_render_audit_total.frame_cycles
printf "CAT bookkeeping=%lu traversal=%lu cover=%lu task_create=%lu dsc_init=%lu style=%lu evaluate=%lu dispatch=%lu dma2d_setup=%lu dma2d_wait=%lu sw_execute=%lu drawbuf_clear=%lu cache=%lu alloc=%lu cleanup=%lu\n", g_render_audit_total.category_cycles[0], g_render_audit_total.category_cycles[1], g_render_audit_total.category_cycles[2], g_render_audit_total.category_cycles[3], g_render_audit_total.category_cycles[4], g_render_audit_total.category_cycles[5], g_render_audit_total.category_cycles[6], g_render_audit_total.category_cycles[7], g_render_audit_total.category_cycles[8], g_render_audit_total.category_cycles[9], g_render_audit_total.category_cycles[10], g_render_audit_total.category_cycles[11], g_render_audit_total.category_cycles[12], g_render_audit_total.category_cycles[13], g_render_audit_total.category_cycles[14]
printf "DRAWTYPE fill_n=%lu fill_cy=%lu border_n=%lu border_cy=%lu boxshadow_n=%lu boxshadow_cy=%lu letter_n=%lu letter_cy=%lu label_n=%lu label_cy=%lu image_n=%lu image_cy=%lu layer_n=%lu layer_cy=%lu line_n=%lu line_cy=%lu\n", g_render_audit_total.draw_type_count[1], g_render_audit_total.draw_type_cycles[1], g_render_audit_total.draw_type_count[2], g_render_audit_total.draw_type_cycles[2], g_render_audit_total.draw_type_count[3], g_render_audit_total.draw_type_cycles[3], g_render_audit_total.draw_type_count[4], g_render_audit_total.draw_type_cycles[4], g_render_audit_total.draw_type_count[5], g_render_audit_total.draw_type_cycles[5], g_render_audit_total.draw_type_count[6], g_render_audit_total.draw_type_cycles[6], g_render_audit_total.draw_type_count[7], g_render_audit_total.draw_type_cycles[7], g_render_audit_total.draw_type_count[8], g_render_audit_total.draw_type_cycles[8]
printf "UNIT dma2d_n=%lu dma2d_cy=%lu sw_n=%lu sw_cy=%lu\n", g_render_audit_total.unit_count[1], g_render_audit_total.unit_cycles[1], g_render_audit_total.unit_count[2], g_render_audit_total.unit_cycles[2]
printf "DMAMODE r2m_n=%lu r2m_px=%lu r2m_cy=%lu blend_n=%lu blend_px=%lu blend_cy=%lu pfc_n=%lu pfc_px=%lu pfc_cy=%lu\n", g_render_audit_total.dma_mode_count[0], g_render_audit_total.dma_mode_pixels[0], g_render_audit_total.dma_mode_cycles[0], g_render_audit_total.dma_mode_count[1], g_render_audit_total.dma_mode_pixels[1], g_render_audit_total.dma_mode_cycles[1], g_render_audit_total.dma_mode_count[2], g_render_audit_total.dma_mode_pixels[2], g_render_audit_total.dma_mode_cycles[2]
printf "DMADEP independent=%lu raw=%lu waw=%lu hard_wait=%lu hideable_wait=%lu\n", g_render_audit_total.dma_dependency_count[0], g_render_audit_total.dma_dependency_count[1], g_render_audit_total.dma_dependency_count[2], g_render_audit_total.dma_hard_wait_cycles, g_render_audit_total.dma_hideable_wait_cycles
printf "PRECLEAR areas=%lu pixels=%lu bytes=%lu opaque_px=%lu cleared_px=%lu skipped_px=%lu dma_err=%lu flags=0x%08lx\n", g_render_audit_total.preclear_area_count, g_render_audit_total.preclear_pixels, g_render_audit_total.preclear_bytes, g_render_audit_total.preclear_opaque_pixels, g_render_audit_total.preclear_cleared_pixels, g_render_audit_total.preclear_skipped_pixels, g_render_audit_total.preclear_dma_error_count, g_render_audit_total.preclear_dma_error_flags
printf "OBJECTS considered=%lu hidden=%lu clip_rejected=%lu drawn=%lu traversal=%lu style_gets=%lu tasks=%lu alloc=%lu free=%lu\n", g_render_audit_total.objects_considered, g_render_audit_total.objects_hidden, g_render_audit_total.objects_clip_rejected, g_render_audit_total.objects_drawn, g_render_audit_total.traversal_calls, g_render_audit_total.style_get_calls, g_render_audit_total.tasks_created, g_render_audit_total.alloc_calls, g_render_audit_total.free_calls
printf "MERGE calls=%lu cycles=%lu comparisons=%lu invalid_before=%lu invalid_after=%lu\n", g_render_audit_merge_calls, g_render_audit_merge_cycles, g_render_audit_merge_comparisons, g_render_audit_invalid_before, g_render_audit_invalid_after
printf "INVALIDATE appends=%lu inv_p_peak=%lu overflow=%lu\n", g_render_audit_invalidate_calls, g_render_audit_inv_p_peak, g_render_audit_inv_overflow_count
printf "LAYOUT calls=%lu passes=%lu cycles=%lu\n", g_render_audit_layout_calls, g_render_audit_layout_passes, g_render_audit_layout_cycles
printf "DIRTY_RING capacity=%lu recorded=%lu index=%lu\n", 24, g_render_audit_dirty_frame_count, g_render_audit_dirty_frame_index

set $df = 0
while $df < g_render_audit_dirty_frame_count && $df < 24
  printf "DIRTY_FRAME ordinal=%lu seq=%lu pre_join_count=%lu joined_count=%lu pre_join_pixels=%lu joined_pixels=%lu truncated=%lu render_cycles=%lu\n", g_render_audit_dirty_frames[$df].ordinal, g_render_audit_dirty_frames[$df].frame_seq, g_render_audit_dirty_frames[$df].pre_join_count, g_render_audit_dirty_frames[$df].joined_count, g_render_audit_dirty_frames[$df].pre_join_pixels, g_render_audit_dirty_frames[$df].joined_pixels, g_render_audit_dirty_frames[$df].truncated, g_render_audit_dirty_frames[$df].render_cycles
  set $di = 0
  while $di < g_render_audit_dirty_frames[$df].pre_join_count && $di < 32
    printf "DIRTY_PRE frame=%lu idx=%lu x1=%d y1=%d x2=%d y2=%d\n", $df, $di, g_render_audit_dirty_frames[$df].pre_join[$di].x1, g_render_audit_dirty_frames[$df].pre_join[$di].y1, g_render_audit_dirty_frames[$df].pre_join[$di].x2, g_render_audit_dirty_frames[$df].pre_join[$di].y2
    set $di = $di + 1
  end
  set $di = 0
  while $di < g_render_audit_dirty_frames[$df].joined_count && $di < 32
    printf "DIRTY_JOINED frame=%lu idx=%lu x1=%d y1=%d x2=%d y2=%d\n", $df, $di, g_render_audit_dirty_frames[$df].joined[$di].x1, g_render_audit_dirty_frames[$df].joined[$di].y1, g_render_audit_dirty_frames[$df].joined[$di].x2, g_render_audit_dirty_frames[$df].joined[$di].y2
    set $di = $di + 1
  end
  set $df = $df + 1
end

printf "SLOT_BBOX_COUNT %lu\n", g_render_audit_object_count
set $oi = 0
while $oi < g_render_audit_object_count
  printf "OBJECT idx=%lu kind=%u inst=%u draw_last=%lu draw_total=%lu\n", $oi, g_render_audit_object_kind[$oi], g_render_audit_object_index[$oi], g_render_audit_object_draw_last[$oi], g_render_audit_object_draw_total[$oi]
  set $oi = $oi + 1
end

set $si = 0
while $si < 12
  printf "SLOT_BBOX idx=%lu before=(%ld,%ld)-(%ld,%ld) after=(%ld,%ld)-(%ld,%ld)\n", $si, g_launcher_slot_bbox_before[$si][0], g_launcher_slot_bbox_before[$si][1], g_launcher_slot_bbox_before[$si][2], g_launcher_slot_bbox_before[$si][3], g_launcher_slot_bbox_after[$si][0], g_launcher_slot_bbox_after[$si][1], g_launcher_slot_bbox_after[$si][2], g_launcher_slot_bbox_after[$si][3]
  set $si = $si + 1
end

set $pi = 0
while $pi < g_render_audit_last.preclear_area_count
  printf "PRECLEAR_RECT idx=%lu x1=%d y1=%d x2=%d y2=%d cleared=%u opaque_cover=%u\n", $pi, g_render_audit_preclear_rects[$pi].x1, g_render_audit_preclear_rects[$pi].y1, g_render_audit_preclear_rects[$pi].x2, g_render_audit_preclear_rects[$pi].y2, g_render_audit_preclear_rects[$pi].cleared, g_render_audit_preclear_rects[$pi].opaque_cover
  set $pi = $pi + 1
end

set $ti = 0
while $ti < g_render_audit_last.rect_count
  printf "DRAW_RECT idx=%lu x1=%d y1=%d x2=%d y2=%d type=%u coverage=%u unit=%u cycles=%lu\n", $ti, g_render_audit_rects[$ti].x1, g_render_audit_rects[$ti].y1, g_render_audit_rects[$ti].x2, g_render_audit_rects[$ti].y2, g_render_audit_rects[$ti].type, g_render_audit_rects[$ti].reserved[0], g_render_audit_rects[$ti].unit, g_render_audit_rects[$ti].exec_cycles
  set $ti = $ti + 1
end

detach
quit
