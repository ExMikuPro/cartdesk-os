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
set variable g_launcher_slot_trace_visual_mode = 0
tbreak DisplayTrace_RenderEnd
continue
monitor resume
shell sleep 1
monitor halt

set variable g_render_audit_preclear_mode = 0
set variable g_scroll_capture_delta_px = 240
set variable g_scroll_capture_steps = 12
set variable g_scroll_capture_step_interval_ms = 0
set variable g_fb_capture_request_step = 0
set variable g_fb_capture_ready = 0
break LauncherScrollCapture_CaptureReady
set variable g_scroll_capture_command = 1
continue
dump binary memory build/preclear-same-run-ab/current-0-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/current-0-b.raw 0xD02EE000 0xD0465000
printf "AB kind=current step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 6
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/current-120-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/current-120-b.raw 0xD02EE000 0xD0465000
printf "AB kind=current step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 12
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/current-240-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/current-240-b.raw 0xD02EE000 0xD0465000
printf "AB kind=current step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x

set variable g_fb_capture_ready = 0
monitor resume
shell sleep 1
monitor halt
set variable g_render_audit_preclear_mode = 3
set variable g_scroll_capture_delta_px = -240
set variable g_scroll_capture_steps = 12
set variable g_fb_capture_request_step = 0
set variable g_fb_capture_ready = 0
set variable g_scroll_capture_command = 1
continue
dump binary memory build/preclear-same-run-ab/dma-240-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-240-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 6
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/dma-120-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-120-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 12
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/dma-0-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-0-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x

set variable g_fb_capture_ready = 0
monitor resume
shell sleep 1
monitor halt
set variable g_scroll_capture_delta_px = 240
set variable g_scroll_capture_steps = 12
set variable g_fb_capture_request_step = 0
set variable g_fb_capture_ready = 0
set variable g_scroll_capture_command = 1
continue
dump binary memory build/preclear-same-run-ab/dma-forward-0-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-forward-0-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma-forward step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 6
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/dma-forward-120-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-forward-120-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma-forward step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
set variable g_fb_capture_request_step = 12
set variable g_fb_capture_ready = 0
continue
dump binary memory build/preclear-same-run-ab/dma-forward-240-a.raw 0xD0177000 0xD02EE000
dump binary memory build/preclear-same-run-ab/dma-forward-240-b.raw 0xD02EE000 0xD0465000
printf "AB kind=dma-forward step=%lu scroll=%ld a_scroll=%ld b_scroll=%ld\n", g_scroll_capture_step, g_scroll_capture_scroll_x, g_scroll_capture_fb_a_scroll_x, g_scroll_capture_fb_b_scroll_x
detach
quit
