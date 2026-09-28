# Run OpenOCD separately, for example: openocd -f CartDeck.cfg
# Then from the repository root:
# arm-none-eabi-gdb -x tools/gdb/ltdc_sync_trace.gdb
#
# This script uses the Debug-LTDC-Sync-Trace preset.  It leaves GDB interactive
# after boot: first wait until the cached slot images are visible, select an A/B
# visual mode, then reset-and-arm one short scroll window.  Press Ctrl-C and
# invoke ltdc_trace_dump to print the counter summary and 256-entry timing tail.

set pagination off
set confirm off
set print pretty on
set print elements 512
file build/Debug-LTDC-Sync-Trace/cartdesk-os.elf
target extended-remote localhost:3333

define ltdc_trace_dump
  printf "\nLTDC/LVGL trace: state=0x%08lx write=%lu count=%lu frame=%lu front=%lu pending=%lu render=%lu presented_seq=%lu pending_seq=%lu reload_pending=%lu flush_pending=%lu\n", g_display_trace_state, g_display_trace_write_index, g_display_trace_count, g_display_frame_seq, g_display_front_fb, g_display_pending_fb, g_display_render_fb, g_display_presented_frame_seq, g_display_pending_frame_seq, g_display_reload_pending, g_display_flush_pending
  printf "reload: request=%lu event=%lu complete_signal=%lu wait_timeout=%lu while_pending=%lu; line=%lu; flush=%lu complete=%lu while_pending=%lu; next_render_before_reload=%lu potential_ownership=%lu\n", g_display_reload_requests, g_display_reload_events, g_display_reload_complete_signals, g_display_reload_wait_timeouts, g_display_reload_while_pending_count, g_display_line_events, g_display_flush_count, g_display_flush_ready_count, g_display_flush_while_pending_count, g_display_next_render_before_reload_count, g_display_potential_ownership_violations
  printf "vsync wait cycles: count=%lu last=%lu min=%lu max=%lu timeout=%lu; pixels=%lu max_area=%lu full_screen=%lu\n", g_display_vsync_wait_count, g_display_vsync_wait_last_cycles, g_display_vsync_wait_min_cycles, g_display_vsync_wait_max_cycles, g_display_vsync_timeouts, g_display_flush_pixels, g_display_largest_flush_pixels, g_display_full_screen_flushes
  printf "render cycles: count=%lu last=%lu min=%lu max=%lu total=%lu (avg=total/count)\n", g_display_render_count, g_display_render_last_cycles, g_display_render_min_cycles, g_display_render_max_cycles, g_display_render_total_cycles
  printf "LTDC errors: fifo_underrun=%lu transfer=%lu other=%lu; FB_A=0x%08lx frame=%lu FB_B=0x%08lx frame=%lu\n", g_display_ltdc_fifo_underruns, g_display_ltdc_transfer_errors, g_display_ltdc_other_errors, g_display_fb_a_address, g_display_fb_a_frame_seq, g_display_fb_b_address, g_display_fb_b_frame_seq
  printf "draw dispatch: dma2d_image=%lu argb=%lu xrgb=%lu blend=%lu pfc_copy=%lu fill=%lu sw_image=%lu alpha_fixed=%lu\n", g_display_dma2d_image_tasks, g_display_dma2d_argb_image_tasks, g_display_dma2d_xrgb_image_tasks, g_display_dma2d_blend_tasks, g_display_dma2d_pfc_tasks, g_display_dma2d_fill_tasks, g_display_sw_image_tasks, g_launcher_slot_trace_alpha_fixed_pixels
  printf "first potential violation: frame=%lu cycle=%lu render=%lu pending=%lu front=%lu; SystemCoreClock=%lu Hz\n", g_display_first_violation_frame, g_display_first_violation_cycle, g_display_first_violation_render_fb, g_display_first_violation_pending_fb, g_display_first_violation_front_fb, SystemCoreClock
  printf "DMA2D image snapshots: 0=full, 1=left clipped, 2=right clipped\n"
  p g_display_dma2d_image_snapshots
  p g_display_trace_ring
end

define ltdc_scroll_prepare
  set variable g_scroll_capture_delta_px = 240
  set variable g_scroll_capture_steps = 12
  set variable g_scroll_capture_step_interval_ms = 0
  set variable g_fb_capture_request_step = 0xffffffff
  set variable g_fb_capture_ready = 0
  printf "Prepared deterministic scroll: +240 px, 12 steps, one rendered frame per step.\n"
end

define ltdc_scroll_start
  set variable g_scroll_capture_command = 1
  printf "Scroll start queued for Launcher_Task app-owner context.\n"
end

define ltdc_scroll_status
  printf "scroll: state=%lu step=%lu frame=%lu start_x=%ld target_x=%ld actual_x=%ld marker_frame=%lu icons_ready=%lu\n", g_scroll_capture_state, g_scroll_capture_step, g_scroll_capture_frame_seq, g_scroll_capture_start_x, g_scroll_capture_target_x, g_scroll_capture_scroll_x, g_scroll_capture_marker_frame_seq, g_scroll_capture_icons_ready
  printf "capture: request_step=%lu ready=%lu frame=%lu A_seq=%lu A_x=%ld B_seq=%lu B_x=%ld presented=%lu pending=%lu render=%lu\n", g_fb_capture_request_step, g_fb_capture_ready, g_fb_capture_frame_seq, g_fb_capture_fb_a_seq, g_fb_capture_fb_a_scroll_x, g_fb_capture_fb_b_seq, g_fb_capture_fb_b_scroll_x, g_fb_capture_presented_seq, g_fb_capture_pending_seq, g_fb_capture_render_seq
end

define ltdc_capture_metadata
  ltdc_scroll_status
  printf "LTDC: SRCR=0x%08lx ISR=0x%08lx CPSR=0x%08lx CDSR=0x%08lx; reload request_seq=%lu complete_seq=%lu\n", g_fb_capture_ltdc_srcr, g_fb_capture_ltdc_isr, g_fb_capture_ltdc_cpsr, g_fb_capture_ltdc_cdsr, g_fb_capture_reload_request_seq, g_fb_capture_reload_complete_seq
end

define ltdc_capture_fb
  shell mkdir -p build/Debug-LTDC-Sync-Trace/captures/manual
  dump binary memory build/Debug-LTDC-Sync-Trace/captures/manual/fb_a.raw 0xD0177000 0xD02EE000
  dump binary memory build/Debug-LTDC-Sync-Trace/captures/manual/fb_b.raw 0xD02EE000 0xD0465000
  printf "Dumped FB_A and FB_B to build/Debug-LTDC-Sync-Trace/captures/manual/.\n"
  ltdc_capture_metadata
end

define ltdc_trace_set_slot_visual
  if $argc != 1
    printf "usage: ltdc_trace_set_slot_visual MODE (0=ARGB image, 1=empty, 2=opaque rect, 3=same pixels XRGB image)\n"
  else
    set variable g_launcher_slot_trace_visual_mode = $arg0
    printf "Slot visual mode %d queued; continue briefly, then halt after the launcher redraws.\n", $arg0
  end
end

define ltdc_trace_arm
  # Firmware consumes this at the next LTDC LineEvent, atomically resetting all
  # trace counters before enabling the ring.  Do this only after images are ready
  # and the chosen visual mode is already visible.
  set variable g_display_trace_command = 1
  printf "Trace arm command queued. Continue and perform one short scroll window.\n"
end

load
monitor reset halt
tbreak lv_port_disp_init
continue
finish

printf "\nBoot paused after lv_port_disp_init. Continue until all target slot images are visible, then Ctrl-C.\n"
printf "Use ltdc_trace_set_slot_visual 0|1|2|3; continue until the mode redraw is complete; Ctrl-C.\n"
printf "Use ltdc_trace_arm; continue; perform one short scroll window; Ctrl-C; then run ltdc_trace_dump.\n"
