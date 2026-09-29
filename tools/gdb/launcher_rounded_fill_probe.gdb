# Identify the large Launcher rounded fill from its live LVGL draw task.
# Start OpenOCD first, then run:
# arm-none-eabi-gdb -q -batch -x tools/gdb/launcher_rounded_fill_probe.gdb
set pagination off
set confirm off
set breakpoint pending on
file build/Debug-LTDC-Full-Render-Audit/cartdesk-os.elf
target extended-remote localhost:3333
monitor reset halt
load
monitor reset halt
tbreak LauncherScrollCapture_IconsReady
continue

# Keep the retained Phase 3 DMA2D pre-clear active.
set variable g_render_audit_preclear_mode = 3
set variable g_launcher_slot_trace_visual_mode = 0
tbreak DisplayTrace_RenderEnd
continue
monitor resume
shell sleep 1
monitor halt

break RenderAudit_DrawExecBegin if type == LV_DRAW_TASK_TYPE_FILL && unit == RENDER_AUDIT_UNIT_SW && x1 == 0 && y1 == 26 && x2 == 799 && y2 == 375
commands
  silent
  up
  set $fill = (lv_draw_fill_dsc_t *)t->draw_dsc
  set $obj = $fill->base.obj
  set $parent = $obj->parent
  printf "ROUNDED_HOTSPOT debug_id=%u registered_obj=%p matches=%u task=%p obj=%p class=%s parent=%p parent_class=%s\n", 1, g_launcher_rounded_fill_object_ptr, $obj == g_launcher_rounded_fill_object_ptr, t, $obj, $obj->class_p->name, $parent, $parent->class_p->name
  printf "ROUNDED_HOTSPOT task_area=(%ld,%ld)-(%ld,%ld) clip=(%ld,%ld)-(%ld,%ld) obj=(%ld,%ld)-(%ld,%ld) parent=(%ld,%ld)-(%ld,%ld)\n", t->area.x1, t->area.y1, t->area.x2, t->area.y2, t->clip_area.x1, t->clip_area.y1, t->clip_area.x2, t->clip_area.y2, $obj->coords.x1, $obj->coords.y1, $obj->coords.x2, $obj->coords.y2, $parent->coords.x1, $parent->coords.y1, $parent->coords.x2, $parent->coords.y2
  printf "ROUNDED_HOTSPOT radius=%ld color=#%02x%02x%02x opa=%u task_opa=%u grad_dir=%u effective_pixels=%ld dma_reject_mask=0x%03x\n", $fill->radius, $fill->color.red, $fill->color.green, $fill->color.blue, $fill->opa, t->opa, $fill->grad.dir, (long)(800 * 350), ($fill->radius != 0 ? 0x002 : 0) | ($fill->grad.dir != LV_GRAD_DIR_NONE ? 0x004 : 0)
  disable $bpnum
end

set variable g_scroll_capture_delta_px = 240
set variable g_scroll_capture_steps = 12
set variable g_scroll_capture_step_interval_ms = 0
set variable g_fb_capture_request_step = 0xffffffff
set variable g_fb_capture_ready = 0
set variable g_display_trace_command = 1
set variable g_scroll_capture_command = 1
continue
monitor resume
shell sleep 4
monitor halt

printf "AUDIT_STATUS scroll_state=%lu step=%lu trace_frames=%lu audit_frames=%lu clock=%lu\n", g_scroll_capture_state, g_scroll_capture_step, g_display_render_count, g_render_audit_frame_count, SystemCoreClock
printf "FAULTS CFSR=0x%08lx HFSR=0x%08lx\n", *(unsigned long*)0xE000ED28, *(unsigned long*)0xE000ED2C
p g_render_audit_total
detach
quit
