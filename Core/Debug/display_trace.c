#include "display_trace.h"

#if CARTDESK_LTDC_SYNC_TRACE_ENABLE

#include <string.h>

#include "stm32h7xx.h"
#include "stm32h7xx_hal_ltdc.h"

#define DISPLAY_TRACE_STATE_ENABLED UINT32_C(0x1)
#define DISPLAY_TRACE_STATE_DWT_READY UINT32_C(0x2)

#define DISPLAY_TRACE_FLAG_SOURCE_LVGL UINT32_C(0x1)
#define DISPLAY_TRACE_FLAG_SOURCE_LEGACY UINT32_C(0x2)
#define DISPLAY_TRACE_FLAG_PRESENTATION_PENDING UINT32_C(0x4)
#define DISPLAY_TRACE_FLAG_POTENTIAL_OWNERSHIP UINT32_C(0x8)
#define DISPLAY_TRACE_FLAG_VSYNC_OBSERVED UINT32_C(0x10)

volatile uint32_t g_display_trace_command;
volatile uint32_t g_display_trace_state;
volatile uint32_t g_display_trace_write_index;
volatile uint32_t g_display_trace_count;
volatile DisplayTraceEvent g_display_trace_ring[DISPLAY_TRACE_CAPACITY];

volatile uint32_t g_display_reload_requests;
volatile uint32_t g_display_reload_events;
volatile uint32_t g_display_reload_complete_signals;
volatile uint32_t g_display_reload_wait_timeouts;
volatile uint32_t g_display_line_events;
volatile uint32_t g_display_flush_count;
volatile uint32_t g_display_flush_ready_count;
volatile uint32_t g_display_vsync_timeouts;
volatile uint32_t g_display_potential_ownership_violations;
volatile uint32_t g_display_reload_while_pending_count;
volatile uint32_t g_display_flush_while_pending_count;
volatile uint32_t g_display_next_render_before_reload_count;
volatile uint32_t g_display_ltdc_fifo_underruns;
volatile uint32_t g_display_ltdc_transfer_errors;
volatile uint32_t g_display_ltdc_other_errors;
volatile uint32_t g_display_frame_seq;
volatile uint32_t g_display_front_fb;
volatile uint32_t g_display_pending_fb;
volatile uint32_t g_display_render_fb;
volatile uint32_t g_display_reload_pending;
volatile uint32_t g_display_flush_pending;
volatile uint32_t g_display_fb_a_frame_seq;
volatile uint32_t g_display_fb_b_frame_seq;
volatile uint32_t g_display_presented_frame_seq;
volatile uint32_t g_display_pending_frame_seq;
volatile uint32_t g_display_fb_a_address;
volatile uint32_t g_display_fb_b_address;
volatile uint32_t g_display_vsync_wait_last_cycles;
volatile uint32_t g_display_vsync_wait_min_cycles;
volatile uint32_t g_display_vsync_wait_max_cycles;
volatile uint32_t g_display_vsync_wait_count;
volatile uint32_t g_display_flush_pixels;
volatile uint32_t g_display_largest_flush_pixels;
volatile uint32_t g_display_full_screen_flushes;
volatile uint32_t g_display_render_last_cycles;
volatile uint32_t g_display_render_min_cycles;
volatile uint32_t g_display_render_max_cycles;
volatile uint32_t g_display_render_total_cycles;
volatile uint32_t g_display_render_count;
volatile uint32_t g_display_dma2d_image_tasks;
volatile uint32_t g_display_dma2d_argb_image_tasks;
volatile uint32_t g_display_dma2d_xrgb_image_tasks;
volatile uint32_t g_display_dma2d_blend_tasks;
volatile uint32_t g_display_dma2d_pfc_tasks;
volatile uint32_t g_display_dma2d_fill_tasks;
volatile uint32_t g_display_sw_image_tasks;
volatile uint32_t g_display_dma2d_image_snapshot_sequence;
volatile DisplayTraceDma2dImageSnapshot
    g_display_dma2d_image_snapshots[DISPLAY_TRACE_IMAGE_SCENE_COUNT];

volatile uint32_t g_display_first_violation_frame;
volatile uint32_t g_display_first_violation_cycle;
volatile uint32_t g_display_first_violation_render_fb;
volatile uint32_t g_display_first_violation_pending_fb;
volatile uint32_t g_display_first_violation_front_fb;

static uint32_t s_vsync_wait_start_cycle;
static uint32_t s_render_start_cycle;

static uint32_t DisplayTrace_IsEnabled(void)
{
    return (g_display_trace_state & DISPLAY_TRACE_STATE_ENABLED) != 0u;
}

static uint32_t DisplayTrace_NowCycles(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u) {
        g_display_trace_state |= DISPLAY_TRACE_STATE_DWT_READY;
        return DWT->CYCCNT;
    }
    return 0u;
}

static uint32_t DisplayTrace_ClassifyFramebuffer(uint32_t address)
{
    if (address == g_display_fb_a_address) {
        return DISPLAY_TRACE_FB_A;
    }
    if (address == g_display_fb_b_address) {
        return DISPLAY_TRACE_FB_B;
    }
    return address == 0u ? DISPLAY_TRACE_FB_NONE : DISPLAY_TRACE_FB_UNKNOWN;
}

static void DisplayTrace_Reset(void)
{
    memset((void *)g_display_trace_ring, 0, sizeof(g_display_trace_ring));
    g_display_trace_write_index = 0u;
    g_display_trace_count = 0u;
    g_display_reload_requests = 0u;
    g_display_reload_events = 0u;
    g_display_reload_complete_signals = 0u;
    g_display_reload_wait_timeouts = 0u;
    g_display_line_events = 0u;
    g_display_flush_count = 0u;
    g_display_flush_ready_count = 0u;
    g_display_vsync_timeouts = 0u;
    g_display_potential_ownership_violations = 0u;
    g_display_reload_while_pending_count = 0u;
    g_display_flush_while_pending_count = 0u;
    g_display_next_render_before_reload_count = 0u;
    g_display_ltdc_fifo_underruns = 0u;
    g_display_ltdc_transfer_errors = 0u;
    g_display_ltdc_other_errors = 0u;
    g_display_frame_seq = 0u;
    g_display_pending_fb = DISPLAY_TRACE_FB_NONE;
    g_display_render_fb = DISPLAY_TRACE_FB_NONE;
    g_display_reload_pending = 0u;
    g_display_flush_pending = 0u;
    g_display_fb_a_frame_seq = 0u;
    g_display_fb_b_frame_seq = 0u;
    g_display_presented_frame_seq = 0u;
    g_display_pending_frame_seq = 0u;
    g_display_vsync_wait_last_cycles = 0u;
    g_display_vsync_wait_min_cycles = UINT32_MAX;
    g_display_vsync_wait_max_cycles = 0u;
    g_display_vsync_wait_count = 0u;
    g_display_flush_pixels = 0u;
    g_display_largest_flush_pixels = 0u;
    g_display_full_screen_flushes = 0u;
    g_display_render_last_cycles = 0u;
    g_display_render_min_cycles = UINT32_MAX;
    g_display_render_max_cycles = 0u;
    g_display_render_total_cycles = 0u;
    g_display_render_count = 0u;
    g_display_dma2d_image_tasks = 0u;
    g_display_dma2d_argb_image_tasks = 0u;
    g_display_dma2d_xrgb_image_tasks = 0u;
    g_display_dma2d_blend_tasks = 0u;
    g_display_dma2d_pfc_tasks = 0u;
    g_display_dma2d_fill_tasks = 0u;
    g_display_sw_image_tasks = 0u;
    g_display_dma2d_image_snapshot_sequence = 0u;
    memset((void *)g_display_dma2d_image_snapshots, 0,
           sizeof(g_display_dma2d_image_snapshots));
    g_display_first_violation_frame = 0u;
    g_display_first_violation_cycle = 0u;
    g_display_first_violation_render_fb = DISPLAY_TRACE_FB_NONE;
    g_display_first_violation_pending_fb = DISPLAY_TRACE_FB_NONE;
    g_display_first_violation_front_fb = DISPLAY_TRACE_FB_NONE;
    s_vsync_wait_start_cycle = 0u;
    s_render_start_cycle = 0u;
}

static void DisplayTrace_Record(DisplayTraceEventType event, uint32_t draw_fb, uint32_t flags)
{
    uint32_t primask;
    uint32_t slot;

    if (DisplayTrace_IsEnabled() == 0u) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    slot = g_display_trace_write_index;
    g_display_trace_ring[slot].cycle = DisplayTrace_NowCycles();
    g_display_trace_ring[slot].ltdc_cpsr = LTDC->CPSR;
    g_display_trace_ring[slot].frame_seq = g_display_frame_seq;
    g_display_trace_ring[slot].event = (uint32_t)event;
    g_display_trace_ring[slot].lv_draw_buf = draw_fb;
    g_display_trace_ring[slot].ltdc_front = g_display_front_fb;
    g_display_trace_ring[slot].pending_fb = g_display_pending_fb;
    g_display_trace_ring[slot].flags = flags;
    g_display_trace_write_index = (slot + 1u) % DISPLAY_TRACE_CAPACITY;
    if (g_display_trace_count < DISPLAY_TRACE_CAPACITY) {
        ++g_display_trace_count;
    }
    __DMB();
    __set_PRIMASK(primask);
}

static void DisplayTrace_SaveFirstViolation(uint32_t render_fb)
{
    if (g_display_first_violation_cycle != 0u) {
        return;
    }
    g_display_first_violation_frame = g_display_frame_seq;
    g_display_first_violation_cycle = DisplayTrace_NowCycles();
    g_display_first_violation_render_fb = render_fb;
    g_display_first_violation_pending_fb = g_display_pending_fb;
    g_display_first_violation_front_fb = g_display_front_fb;
}

void DisplayTrace_Poll(void)
{
    uint32_t command = g_display_trace_command;

    if (command == DISPLAY_TRACE_COMMAND_RESET_AND_ENABLE) {
        g_display_trace_command = DISPLAY_TRACE_COMMAND_NONE;
        DisplayTrace_Reset();
        g_display_trace_state |= DISPLAY_TRACE_STATE_ENABLED;
        (void)DisplayTrace_NowCycles();
    }
    else if (command == DISPLAY_TRACE_COMMAND_DISABLE) {
        g_display_trace_command = DISPLAY_TRACE_COMMAND_NONE;
        g_display_trace_state &= ~DISPLAY_TRACE_STATE_ENABLED;
    }
}

void DisplayTrace_ConfigureFramebuffers(uint32_t fb_a, uint32_t fb_b, uint32_t front)
{
    g_display_fb_a_address = fb_a;
    g_display_fb_b_address = fb_b;
    g_display_front_fb = DisplayTrace_ClassifyFramebuffer(front);
}

void DisplayTrace_RefreshBegin(uint32_t render_fb_address)
{
    uint32_t render_fb = DisplayTrace_ClassifyFramebuffer(render_fb_address);
    uint32_t flags = 0u;

    ++g_display_frame_seq;
    g_display_render_fb = render_fb;
    if (g_display_reload_pending != 0u) {
        ++g_display_next_render_before_reload_count;
        flags |= DISPLAY_TRACE_FLAG_PRESENTATION_PENDING;
        if (render_fb == g_display_pending_fb) {
            ++g_display_potential_ownership_violations;
            flags |= DISPLAY_TRACE_FLAG_POTENTIAL_OWNERSHIP;
            DisplayTrace_SaveFirstViolation(render_fb);
        }
    }
    DisplayTrace_Record(DISPLAY_TRACE_NEXT_RENDER_BEGIN, render_fb, flags);
}

void DisplayTrace_RenderBegin(uint32_t render_fb_address)
{
    uint32_t render_fb = DisplayTrace_ClassifyFramebuffer(render_fb_address);
    g_display_render_fb = render_fb;
    s_render_start_cycle = DisplayTrace_NowCycles();
    DisplayTrace_Record(DISPLAY_TRACE_RENDER_BEGIN, render_fb,
                        g_display_reload_pending != 0u ? DISPLAY_TRACE_FLAG_PRESENTATION_PENDING : 0u);
}

void DisplayTrace_RenderEnd(uint32_t render_fb_address)
{
    if (s_render_start_cycle != 0u) {
        uint32_t elapsed = DisplayTrace_NowCycles() - s_render_start_cycle;
        g_display_render_last_cycles = elapsed;
        if (elapsed < g_display_render_min_cycles) {
            g_display_render_min_cycles = elapsed;
        }
        if (elapsed > g_display_render_max_cycles) {
            g_display_render_max_cycles = elapsed;
        }
        g_display_render_total_cycles += elapsed;
        ++g_display_render_count;
        s_render_start_cycle = 0u;
    }
    DisplayTrace_Record(DISPLAY_TRACE_RENDER_END, DisplayTrace_ClassifyFramebuffer(render_fb_address), 0u);
}

void DisplayTrace_FlushEnter(uint32_t draw_fb_address, uint32_t area_px, uint32_t full_screen)
{
    uint32_t draw_fb = DisplayTrace_ClassifyFramebuffer(draw_fb_address);
    uint32_t flags = 0u;

    ++g_display_flush_count;
    g_display_flush_pending = 1u;
    g_display_render_fb = draw_fb;
    g_display_flush_pixels += area_px;
    if (area_px > g_display_largest_flush_pixels) {
        g_display_largest_flush_pixels = area_px;
    }
    if (full_screen != 0u) {
        ++g_display_full_screen_flushes;
    }
    if (draw_fb == DISPLAY_TRACE_FB_A) {
        g_display_fb_a_frame_seq = g_display_frame_seq;
    }
    else if (draw_fb == DISPLAY_TRACE_FB_B) {
        g_display_fb_b_frame_seq = g_display_frame_seq;
    }
    if (g_display_reload_pending != 0u) {
        ++g_display_flush_while_pending_count;
        flags |= DISPLAY_TRACE_FLAG_PRESENTATION_PENDING;
        if (draw_fb == g_display_pending_fb) {
            ++g_display_potential_ownership_violations;
            flags |= DISPLAY_TRACE_FLAG_POTENTIAL_OWNERSHIP;
            DisplayTrace_SaveFirstViolation(draw_fb);
        }
    }
    DisplayTrace_Record(DISPLAY_TRACE_FLUSH_ENTER, draw_fb, flags);
}

void DisplayTrace_VsyncWaitBegin(void)
{
    s_vsync_wait_start_cycle = DisplayTrace_NowCycles();
    DisplayTrace_Record(DISPLAY_TRACE_VSYNC_WAIT_BEGIN, g_display_render_fb, 0u);
}

void DisplayTrace_VsyncWaitEnd(uint32_t observed)
{
    uint32_t elapsed = DisplayTrace_NowCycles() - s_vsync_wait_start_cycle;
    g_display_vsync_wait_last_cycles = elapsed;
    if (elapsed < g_display_vsync_wait_min_cycles) {
        g_display_vsync_wait_min_cycles = elapsed;
    }
    if (elapsed > g_display_vsync_wait_max_cycles) {
        g_display_vsync_wait_max_cycles = elapsed;
    }
    ++g_display_vsync_wait_count;
    if (observed != 0u) {
        DisplayTrace_Record(DISPLAY_TRACE_VSYNC_OBSERVED, g_display_render_fb,
                            DISPLAY_TRACE_FLAG_VSYNC_OBSERVED);
    }
    else {
        ++g_display_vsync_timeouts;
        DisplayTrace_Record(DISPLAY_TRACE_VSYNC_TIMEOUT, g_display_render_fb, 0u);
    }
}

void DisplayTrace_SetAddressBegin(uint32_t draw_fb_address)
{
    DisplayTrace_Record(DISPLAY_TRACE_SET_ADDRESS_BEGIN,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address), 0u);
}

void DisplayTrace_SetAddressEnd(uint32_t draw_fb_address)
{
    DisplayTrace_Record(DISPLAY_TRACE_SET_ADDRESS_END,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address), 0u);
}

void DisplayTrace_SetAddressNoReload(uint32_t draw_fb_address)
{
    DisplayTrace_Record(DISPLAY_TRACE_SET_ADDRESS_NO_RELOAD,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address), 0u);
}

void DisplayTrace_LvglReloadRequest(uint32_t draw_fb_address)
{
    uint32_t draw_fb = DisplayTrace_ClassifyFramebuffer(draw_fb_address);
    uint32_t flags = DISPLAY_TRACE_FLAG_SOURCE_LVGL;

    if (g_display_reload_pending != 0u) {
        ++g_display_reload_while_pending_count;
        flags |= DISPLAY_TRACE_FLAG_PRESENTATION_PENDING;
    }
    ++g_display_reload_requests;
    g_display_pending_fb = draw_fb;
    if (draw_fb == DISPLAY_TRACE_FB_A) {
        g_display_pending_frame_seq = g_display_fb_a_frame_seq;
    }
    else if (draw_fb == DISPLAY_TRACE_FB_B) {
        g_display_pending_frame_seq = g_display_fb_b_frame_seq;
    }
    else {
        g_display_pending_frame_seq = 0u;
    }
    g_display_reload_pending = 1u;
    DisplayTrace_Record(DISPLAY_TRACE_VBLANK_RELOAD_REQUEST, draw_fb, flags);
}

void DisplayTrace_ReloadCompleteSignal(uint32_t draw_fb_address)
{
    ++g_display_reload_complete_signals;
    DisplayTrace_Record(DISPLAY_TRACE_RELOAD_COMPLETE_SIGNAL,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address), 0u);
}

void DisplayTrace_FlushWaitBegin(uint32_t draw_fb_address)
{
    DisplayTrace_Record(DISPLAY_TRACE_FLUSH_WAIT_BEGIN,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address),
                        DISPLAY_TRACE_FLAG_PRESENTATION_PENDING);
}

void DisplayTrace_FlushWaitEnd(uint32_t draw_fb_address)
{
    DisplayTrace_Record(DISPLAY_TRACE_FLUSH_WAIT_END,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address), 0u);
}

void DisplayTrace_FlushComplete(void)
{
    ++g_display_flush_ready_count;
    g_display_flush_pending = 0u;
    DisplayTrace_Record(DISPLAY_TRACE_FLUSH_COMPLETE, g_display_render_fb, 0u);
}

void DisplayTrace_ReloadWaitTimeout(uint32_t draw_fb_address)
{
    ++g_display_reload_wait_timeouts;
    DisplayTrace_Record(DISPLAY_TRACE_RELOAD_WAIT_TIMEOUT,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address),
                        DISPLAY_TRACE_FLAG_PRESENTATION_PENDING);
}

void DisplayTrace_ReloadRejected(uint32_t draw_fb_address)
{
    ++g_display_reload_while_pending_count;
    DisplayTrace_Record(DISPLAY_TRACE_RELOAD_REJECTED,
                        DisplayTrace_ClassifyFramebuffer(draw_fb_address),
                        DISPLAY_TRACE_FLAG_PRESENTATION_PENDING);
}

void DisplayTrace_LegacyReloadRequest(void)
{
    ++g_display_reload_requests;
    DisplayTrace_Record(DISPLAY_TRACE_VBLANK_RELOAD_REQUEST, g_display_render_fb,
                        DISPLAY_TRACE_FLAG_SOURCE_LEGACY);
}

void DisplayTrace_FlushReady(void)
{
    DisplayTrace_FlushComplete();
}

void DisplayTrace_LtdcLineEvent(void)
{
    ++g_display_line_events;
    DisplayTrace_Record(DISPLAY_TRACE_LTDC_LINE_EVENT, g_display_render_fb, 0u);
}

void DisplayTrace_LtdcReloadEvent(void)
{
    ++g_display_reload_events;
    DisplayTrace_Record(DISPLAY_TRACE_LTDC_RELOAD_EVENT, g_display_render_fb,
                        g_display_reload_pending != 0u ? DISPLAY_TRACE_FLAG_PRESENTATION_PENDING : 0u);

    /* RR is global rather than Layer-1-specific.  This is a software acknowledgement
     * of the latest traced LVGL request, not a claim that CFBAR is a readable active
     * scanout register. */
    if (g_display_reload_pending != 0u) {
        g_display_front_fb = g_display_pending_fb;
        g_display_presented_frame_seq = g_display_pending_frame_seq;
        g_display_pending_fb = DISPLAY_TRACE_FB_NONE;
        g_display_pending_frame_seq = 0u;
        g_display_reload_pending = 0u;
    }
}

void DisplayTrace_LtdcError(uint32_t error_code, uint32_t isr)
{
    if ((error_code & HAL_LTDC_ERROR_FU) != 0u || (isr & LTDC_ISR_FUIF) != 0u) {
        ++g_display_ltdc_fifo_underruns;
    }
    if ((error_code & HAL_LTDC_ERROR_TE) != 0u || (isr & LTDC_ISR_TERRIF) != 0u) {
        ++g_display_ltdc_transfer_errors;
    }
    if ((error_code & (HAL_LTDC_ERROR_FU | HAL_LTDC_ERROR_TE)) == 0u) {
        ++g_display_ltdc_other_errors;
    }
}

void DisplayTrace_Dma2dImageDraw(uint32_t mode, uint32_t image_cf, uint32_t output_cf,
                                 uint32_t source_stride, uint32_t output_stride,
                                 int32_t image_x1, int32_t image_y1,
                                 int32_t image_x2, int32_t image_y2,
                                 int32_t clip_x1, int32_t clip_y1,
                                 int32_t clip_x2, int32_t clip_y2)
{
    ++g_display_dma2d_image_tasks;
    if (image_cf == UINT32_C(0x10)) { /* LV_COLOR_FORMAT_ARGB8888 */
        ++g_display_dma2d_argb_image_tasks;
    }
    else if (image_cf == UINT32_C(0x11)) { /* LV_COLOR_FORMAT_XRGB8888 */
        ++g_display_dma2d_xrgb_image_tasks;
    }
    if (mode == 2u) { /* DMA2D M2M_BLEND */
        ++g_display_dma2d_blend_tasks;
    }
    else if (mode == 0u || mode == 1u) { /* DMA2D M2M / M2M_PFC */
        ++g_display_dma2d_pfc_tasks;
    }

    /* Keep the register snapshots specific to the 200 x 200 Launcher preview.
     * Other LVGL images still contribute to the dispatch counters. */
    if ((image_x2 - image_x1 + 1) != 200 ||
        (image_y2 - image_y1 + 1) != 200 ||
        source_stride != 800u) {
        return;
    }

    uint32_t scene = DISPLAY_TRACE_IMAGE_SCENE_FULL;
    if (clip_x1 > image_x1) {
        scene = DISPLAY_TRACE_IMAGE_SCENE_CLIPPED_LEFT;
    }
    else if (clip_x2 < image_x2) {
        scene = DISPLAY_TRACE_IMAGE_SCENE_CLIPPED_RIGHT;
    }

    volatile DisplayTraceDma2dImageSnapshot *snapshot =
        &g_display_dma2d_image_snapshots[scene];
    snapshot->sequence = ++g_display_dma2d_image_snapshot_sequence;
    snapshot->scene = scene;
    snapshot->mode = mode;
    snapshot->image_cf = image_cf;
    snapshot->output_cf = output_cf;
    snapshot->source_stride = source_stride;
    snapshot->output_stride = output_stride;
    snapshot->image_x1 = image_x1;
    snapshot->image_y1 = image_y1;
    snapshot->image_x2 = image_x2;
    snapshot->image_y2 = image_y2;
    snapshot->clip_x1 = clip_x1;
    snapshot->clip_y1 = clip_y1;
    snapshot->clip_x2 = clip_x2;
    snapshot->clip_y2 = clip_y2;
    snapshot->cr = DMA2D->CR;
    snapshot->fgmar = DMA2D->FGMAR;
    snapshot->fgor = DMA2D->FGOR;
    snapshot->bgmar = DMA2D->BGMAR;
    snapshot->bgor = DMA2D->BGOR;
    snapshot->omar = DMA2D->OMAR;
    snapshot->oor = DMA2D->OOR;
    snapshot->fgpfccr = DMA2D->FGPFCCR;
    snapshot->bgpfccr = DMA2D->BGPFCCR;
    snapshot->opfccr = DMA2D->OPFCCR;
    snapshot->nlr = DMA2D->NLR;
    __DMB();
}

void DisplayTrace_Dma2dFill(void)
{
    ++g_display_dma2d_fill_tasks;
}

void DisplayTrace_SwImage(void)
{
    ++g_display_sw_image_tasks;
}

#endif /* CARTDESK_LTDC_SYNC_TRACE_ENABLE */
