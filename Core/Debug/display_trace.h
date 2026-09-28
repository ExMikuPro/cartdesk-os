#ifndef CARTDESK_DISPLAY_TRACE_H
#define CARTDESK_DISPLAY_TRACE_H

#include <stdint.h>

#ifndef CARTDESK_LTDC_SYNC_TRACE_ENABLE
#define CARTDESK_LTDC_SYNC_TRACE_ENABLE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DISPLAY_TRACE_FB_NONE = 0u,
    DISPLAY_TRACE_FB_A = 1u,
    DISPLAY_TRACE_FB_B = 2u,
    DISPLAY_TRACE_FB_UNKNOWN = 3u
};

typedef enum {
    DISPLAY_TRACE_RENDER_BEGIN = 1u,
    DISPLAY_TRACE_RENDER_END,
    DISPLAY_TRACE_FLUSH_ENTER,
    DISPLAY_TRACE_VSYNC_WAIT_BEGIN,
    DISPLAY_TRACE_VSYNC_OBSERVED,
    DISPLAY_TRACE_VSYNC_TIMEOUT,
    DISPLAY_TRACE_SET_ADDRESS_BEGIN,
    DISPLAY_TRACE_SET_ADDRESS_END,
    DISPLAY_TRACE_VBLANK_RELOAD_REQUEST,
    DISPLAY_TRACE_FLUSH_READY,
    DISPLAY_TRACE_LTDC_LINE_EVENT,
    DISPLAY_TRACE_LTDC_RELOAD_EVENT,
    DISPLAY_TRACE_NEXT_RENDER_BEGIN,
    DISPLAY_TRACE_SET_ADDRESS_NO_RELOAD,
    DISPLAY_TRACE_RELOAD_COMPLETE_SIGNAL,
    DISPLAY_TRACE_FLUSH_WAIT_BEGIN,
    DISPLAY_TRACE_FLUSH_WAIT_END,
    DISPLAY_TRACE_FLUSH_COMPLETE,
    DISPLAY_TRACE_RELOAD_WAIT_TIMEOUT,
    DISPLAY_TRACE_RELOAD_REJECTED,
    DISPLAY_TRACE_BUFFER_SYNC_BEGIN,
    DISPLAY_TRACE_BUFFER_SYNC_END
} DisplayTraceEventType;

typedef struct {
    uint32_t cycle;
    uint32_t ltdc_cpsr;
    uint32_t frame_seq;
    uint32_t event;
    uint32_t lv_draw_buf;
    uint32_t ltdc_front;
    uint32_t pending_fb;
    uint32_t flags;
    uint32_t value;
    uint32_t aux;
} DisplayTraceEvent;

enum {
    DISPLAY_TRACE_IMAGE_SCENE_FULL = 0u,
    DISPLAY_TRACE_IMAGE_SCENE_CLIPPED_LEFT = 1u,
    DISPLAY_TRACE_IMAGE_SCENE_CLIPPED_RIGHT = 2u,
    DISPLAY_TRACE_IMAGE_SCENE_COUNT = 3u
};

typedef struct {
    uint32_t sequence;
    uint32_t scene;
    uint32_t mode;
    uint32_t image_cf;
    uint32_t output_cf;
    uint32_t source_stride;
    uint32_t output_stride;
    int32_t image_x1;
    int32_t image_y1;
    int32_t image_x2;
    int32_t image_y2;
    int32_t clip_x1;
    int32_t clip_y1;
    int32_t clip_x2;
    int32_t clip_y2;
    uint32_t cr;
    uint32_t fgmar;
    uint32_t fgor;
    uint32_t bgmar;
    uint32_t bgor;
    uint32_t omar;
    uint32_t oor;
    uint32_t fgpfccr;
    uint32_t bgpfccr;
    uint32_t opfccr;
    uint32_t nlr;
} DisplayTraceDma2dImageSnapshot;

#if CARTDESK_LTDC_SYNC_TRACE_ENABLE

#define DISPLAY_TRACE_CAPACITY 512u

enum {
    DISPLAY_TRACE_COMMAND_NONE = 0u,
    DISPLAY_TRACE_COMMAND_RESET_AND_ENABLE = 1u,
    DISPLAY_TRACE_COMMAND_DISABLE = 2u
};

/* GDB mailbox: write only g_display_trace_command; firmware owns all results. */
extern volatile uint32_t g_display_trace_command;
extern volatile uint32_t g_display_trace_state;
extern volatile uint32_t g_display_trace_write_index;
extern volatile uint32_t g_display_trace_count;
extern volatile DisplayTraceEvent g_display_trace_ring[DISPLAY_TRACE_CAPACITY];

extern volatile uint32_t g_display_reload_requests;
extern volatile uint32_t g_display_reload_events;
extern volatile uint32_t g_display_reload_complete_signals;
extern volatile uint32_t g_display_reload_wait_timeouts;
extern volatile uint32_t g_display_line_events;
extern volatile uint32_t g_display_flush_count;
extern volatile uint32_t g_display_flush_ready_count;
extern volatile uint32_t g_display_vsync_timeouts;
extern volatile uint32_t g_display_potential_ownership_violations;
extern volatile uint32_t g_display_reload_while_pending_count;
extern volatile uint32_t g_display_flush_while_pending_count;
extern volatile uint32_t g_display_next_render_before_reload_count;
extern volatile uint32_t g_display_ltdc_fifo_underruns;
extern volatile uint32_t g_display_ltdc_transfer_errors;
extern volatile uint32_t g_display_ltdc_other_errors;
extern volatile uint32_t g_display_frame_seq;
extern volatile uint32_t g_display_front_fb;
extern volatile uint32_t g_display_pending_fb;
extern volatile uint32_t g_display_render_fb;
extern volatile uint32_t g_display_reload_pending;
extern volatile uint32_t g_display_flush_pending;
extern volatile uint32_t g_display_fb_a_frame_seq;
extern volatile uint32_t g_display_fb_b_frame_seq;
extern volatile uint32_t g_display_presented_frame_seq;
extern volatile uint32_t g_display_pending_frame_seq;
extern volatile uint32_t g_display_fb_a_address;
extern volatile uint32_t g_display_fb_b_address;
extern volatile uint32_t g_display_vsync_wait_last_cycles;
extern volatile uint32_t g_display_vsync_wait_min_cycles;
extern volatile uint32_t g_display_vsync_wait_max_cycles;
extern volatile uint32_t g_display_vsync_wait_count;
extern volatile uint32_t g_display_flush_pixels;
extern volatile uint32_t g_display_largest_flush_pixels;
extern volatile uint32_t g_display_full_screen_flushes;
extern volatile uint32_t g_display_render_last_cycles;
extern volatile uint32_t g_display_render_min_cycles;
extern volatile uint32_t g_display_render_max_cycles;
extern volatile uint32_t g_display_render_total_cycles;
extern volatile uint32_t g_display_render_count;
extern volatile uint32_t g_display_dma2d_image_tasks;
extern volatile uint32_t g_display_dma2d_argb_image_tasks;
extern volatile uint32_t g_display_dma2d_xrgb_image_tasks;
extern volatile uint32_t g_display_dma2d_blend_tasks;
extern volatile uint32_t g_display_dma2d_pfc_tasks;
extern volatile uint32_t g_display_dma2d_fill_tasks;
extern volatile uint32_t g_display_sw_image_tasks;
extern volatile uint32_t g_display_dma2d_image_snapshot_sequence;
extern volatile DisplayTraceDma2dImageSnapshot
    g_display_dma2d_image_snapshots[DISPLAY_TRACE_IMAGE_SCENE_COUNT];

extern volatile uint32_t g_display_first_violation_frame;
extern volatile uint32_t g_display_first_violation_cycle;
extern volatile uint32_t g_display_first_violation_render_fb;
extern volatile uint32_t g_display_first_violation_pending_fb;
extern volatile uint32_t g_display_first_violation_front_fb;

void DisplayTrace_Poll(void);
void DisplayTrace_ConfigureFramebuffers(uint32_t fb_a, uint32_t fb_b, uint32_t front);
void DisplayTrace_RefreshBegin(uint32_t render_fb);
void DisplayTrace_RenderBegin(uint32_t render_fb);
void DisplayTrace_RenderEnd(uint32_t render_fb);
void DisplayTrace_BufferSyncBegin(void);
void DisplayTrace_BufferSyncArea(uint32_t area_px);
void DisplayTrace_BufferSyncEnd(void);
void DisplayTrace_FlushEnter(uint32_t draw_fb, uint32_t area_px, uint32_t full_screen);
void DisplayTrace_VsyncWaitBegin(void);
void DisplayTrace_VsyncWaitEnd(uint32_t observed);
void DisplayTrace_SetAddressBegin(uint32_t draw_fb);
void DisplayTrace_SetAddressEnd(uint32_t draw_fb);
void DisplayTrace_SetAddressNoReload(uint32_t draw_fb);
void DisplayTrace_LvglReloadRequest(uint32_t draw_fb);
void DisplayTrace_ReloadCompleteSignal(uint32_t draw_fb);
void DisplayTrace_FlushWaitBegin(uint32_t draw_fb);
void DisplayTrace_FlushWaitEnd(uint32_t draw_fb);
void DisplayTrace_FlushComplete(void);
void DisplayTrace_ReloadWaitTimeout(uint32_t draw_fb);
void DisplayTrace_ReloadRejected(uint32_t draw_fb);
void DisplayTrace_LegacyReloadRequest(void);
void DisplayTrace_FlushReady(void);
void DisplayTrace_LtdcLineEvent(void);
void DisplayTrace_LtdcReloadEvent(void);
void DisplayTrace_LtdcError(uint32_t error_code, uint32_t isr);
void DisplayTrace_Dma2dImageDraw(uint32_t mode, uint32_t image_cf, uint32_t output_cf,
                                 uint32_t source_stride, uint32_t output_stride,
                                 int32_t image_x1, int32_t image_y1,
                                 int32_t image_x2, int32_t image_y2,
                                 int32_t clip_x1, int32_t clip_y1,
                                 int32_t clip_x2, int32_t clip_y2);
void DisplayTrace_Dma2dFill(void);
void DisplayTrace_SwImage(void);

#else

static inline void DisplayTrace_Poll(void) {}
static inline void DisplayTrace_ConfigureFramebuffers(uint32_t fb_a, uint32_t fb_b, uint32_t front)
{
    (void)fb_a;
    (void)fb_b;
    (void)front;
}
static inline void DisplayTrace_RefreshBegin(uint32_t render_fb) { (void)render_fb; }
static inline void DisplayTrace_RenderBegin(uint32_t render_fb) { (void)render_fb; }
static inline void DisplayTrace_RenderEnd(uint32_t render_fb) { (void)render_fb; }
static inline void DisplayTrace_BufferSyncBegin(void) {}
static inline void DisplayTrace_BufferSyncArea(uint32_t area_px) { (void)area_px; }
static inline void DisplayTrace_BufferSyncEnd(void) {}
static inline void DisplayTrace_FlushEnter(uint32_t draw_fb, uint32_t area_px, uint32_t full_screen)
{
    (void)draw_fb;
    (void)area_px;
    (void)full_screen;
}
static inline void DisplayTrace_VsyncWaitBegin(void) {}
static inline void DisplayTrace_VsyncWaitEnd(uint32_t observed) { (void)observed; }
static inline void DisplayTrace_SetAddressBegin(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_SetAddressEnd(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_SetAddressNoReload(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_LvglReloadRequest(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_ReloadCompleteSignal(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_FlushWaitBegin(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_FlushWaitEnd(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_FlushComplete(void) {}
static inline void DisplayTrace_ReloadWaitTimeout(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_ReloadRejected(uint32_t draw_fb) { (void)draw_fb; }
static inline void DisplayTrace_LegacyReloadRequest(void) {}
static inline void DisplayTrace_FlushReady(void) {}
static inline void DisplayTrace_LtdcLineEvent(void) {}
static inline void DisplayTrace_LtdcReloadEvent(void) {}
static inline void DisplayTrace_LtdcError(uint32_t error_code, uint32_t isr)
{
    (void)error_code;
    (void)isr;
}
static inline void DisplayTrace_Dma2dImageDraw(uint32_t mode, uint32_t image_cf,
                                               uint32_t output_cf,
                                               uint32_t source_stride,
                                               uint32_t output_stride,
                                               int32_t image_x1, int32_t image_y1,
                                               int32_t image_x2, int32_t image_y2,
                                               int32_t clip_x1, int32_t clip_y1,
                                               int32_t clip_x2, int32_t clip_y2)
{
    (void)mode;
    (void)image_cf;
    (void)output_cf;
    (void)source_stride;
    (void)output_stride;
    (void)image_x1;
    (void)image_y1;
    (void)image_x2;
    (void)image_y2;
    (void)clip_x1;
    (void)clip_y1;
    (void)clip_x2;
    (void)clip_y2;
}
static inline void DisplayTrace_Dma2dFill(void) {}
static inline void DisplayTrace_SwImage(void) {}

#endif

#ifdef __cplusplus
}
#endif

#endif /* CARTDESK_DISPLAY_TRACE_H */
