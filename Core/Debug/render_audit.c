#include "render_audit.h"

#if CARTDESK_RENDER_AUDIT_ENABLE

#include <string.h>

#include "FreeRTOS.h"
#include "stm32h7xx.h"
#include "task.h"

#define RENDER_AUDIT_STACK_DEPTH 20u

volatile uint32_t g_render_audit_enabled;
volatile uint32_t g_render_audit_frame_count;
volatile RenderAuditFrame g_render_audit_last;
volatile RenderAuditFrame g_render_audit_total;
volatile RenderAuditRect g_render_audit_rects[RENDER_AUDIT_RECT_CAPACITY];
volatile RenderAuditPreclearRect g_render_audit_preclear_rects[RENDER_AUDIT_PRECLEAR_CAPACITY];
volatile uint32_t g_render_audit_preclear_mode;
volatile uint32_t g_render_audit_merge_calls;
volatile uint32_t g_render_audit_merge_cycles;
volatile uint32_t g_render_audit_merge_comparisons;
volatile uint32_t g_render_audit_invalid_before;
volatile uint32_t g_render_audit_invalid_after;
volatile uint32_t g_render_audit_invalidate_calls;
volatile uint32_t g_render_audit_inv_p_peak;
volatile uint32_t g_render_audit_inv_overflow_count;
volatile uint32_t g_render_audit_dirty_frame_count;
volatile uint32_t g_render_audit_dirty_frame_index;
volatile RenderAuditDirtyFrame
    g_render_audit_dirty_frames[RENDER_AUDIT_DIRTY_FRAME_CAPACITY];
volatile uint32_t g_render_audit_layout_calls;
volatile uint32_t g_render_audit_layout_passes;
volatile uint32_t g_render_audit_layout_cycles;
volatile uint32_t g_render_audit_object_count;
volatile uintptr_t g_render_audit_object_ptr[RENDER_AUDIT_OBJECT_CAPACITY];
volatile uint8_t g_render_audit_object_kind[RENDER_AUDIT_OBJECT_CAPACITY];
volatile uint8_t g_render_audit_object_index[RENDER_AUDIT_OBJECT_CAPACITY];
volatile uint32_t g_render_audit_object_draw_last[RENDER_AUDIT_OBJECT_CAPACITY];
volatile uint32_t g_render_audit_object_draw_total[RENDER_AUDIT_OBJECT_CAPACITY];

static RenderAuditFrame s_frame;
static RenderAuditCategory s_stack[RENDER_AUDIT_STACK_DEPTH];
static uint32_t s_stack_depth;
static uint32_t s_frame_start;
static uint32_t s_category_start;
static uint32_t s_task_start;
static uint32_t s_irq_start;
static uint32_t s_draw_start[RENDER_AUDIT_UNIT_COUNT];
static RenderAuditRect s_draw_rect[RENDER_AUDIT_UNIT_COUNT];
static uint32_t s_dma_start;
static uint32_t s_dma_wait_start;
static uint32_t s_dma_setup_start;
static uint8_t s_dma_setup_kind;
static uint8_t s_dma_mode;
static uint8_t s_dma_kind;
static uint8_t s_dma_kind_hint;
static RenderAuditRect s_dma_region;
static RenderAuditRect s_dma_region_candidate;
static uint32_t s_dma_pending_wait_cycles;
static uint8_t s_dma_dependency_pending;
static uint8_t s_frame_active;
static uint8_t s_task_accounting;
static uint8_t s_irq_active;
static uint8_t s_draw_type;
static uint8_t s_draw_unit;
static RenderAuditTaskClass s_task_class;
static TaskHandle_t s_swdraw_task_handle;
static uint32_t s_dirty_pending_index;
static uint8_t s_dirty_pending;

static uint32_t now_cycles(void)
{
    return DWT->CYCCNT;
}

static RenderAuditTaskClass classify_current_task(void)
{
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    const char *name = pcTaskGetName(NULL);
    if(s_swdraw_task_handle != NULL && current == s_swdraw_task_handle) return RENDER_AUDIT_TASK_SWDRAW;
    if(name == NULL) return RENDER_AUDIT_TASK_OTHER;
    if(strcmp(name, "app") == 0) return RENDER_AUDIT_TASK_APP;
    if(strcmp(name, "swdraw") == 0) return RENDER_AUDIT_TASK_SWDRAW;
    if(strcmp(name, "audio") == 0) return RENDER_AUDIT_TASK_AUDIO;
    if(strcmp(name, "io") == 0) return RENDER_AUDIT_TASK_IO;
    if(strcmp(name, "background") == 0) return RENDER_AUDIT_TASK_BACKGROUND;
    if(strncmp(name, "IDLE", 4u) == 0) return RENDER_AUDIT_TASK_IDLE;
    return RENDER_AUDIT_TASK_OTHER;
}

static uint32_t category_is_profiled_task(void)
{
    return s_task_class == RENDER_AUDIT_TASK_APP ||
           s_task_class == RENDER_AUDIT_TASK_SWDRAW;
}

static void stop_category(uint32_t now)
{
    if(s_category_start != 0u && s_stack_depth != 0u && category_is_profiled_task()) {
        s_frame.category_cycles[s_stack[s_stack_depth - 1u]] += now - s_category_start;
    }
    s_category_start = 0u;
}

static void start_category(uint32_t now)
{
    if(s_stack_depth != 0u && category_is_profiled_task() && s_irq_active == 0u) {
        s_category_start = now;
    }
}

static void stop_task(uint32_t now)
{
    if(s_task_accounting != 0u) {
        s_frame.task_cycles[s_task_class] += now - s_task_start;
        s_task_accounting = 0u;
    }
}

static void start_task(uint32_t now)
{
    if(s_irq_active == 0u) {
        s_task_start = now;
        s_task_accounting = 1u;
    }
}

static void add_frame(volatile RenderAuditFrame *dst, const RenderAuditFrame *src)
{
    uint32_t i;
    dst->frame_seq = src->frame_seq;
    dst->frame_cycles += src->frame_cycles;
    for(i = 0u; i < RENDER_AUDIT_CAT_COUNT; ++i) dst->category_cycles[i] += src->category_cycles[i];
    for(i = 0u; i < RENDER_AUDIT_TASK_COUNT; ++i) dst->task_cycles[i] += src->task_cycles[i];
    for(i = 0u; i < RENDER_AUDIT_IRQ_COUNT; ++i) {
        dst->irq_cycles[i] += src->irq_cycles[i];
        dst->irq_count[i] += src->irq_count[i];
    }
    for(i = 0u; i < RENDER_AUDIT_DRAW_TYPE_COUNT; ++i) {
        dst->draw_type_cycles[i] += src->draw_type_cycles[i];
        dst->draw_type_count[i] += src->draw_type_count[i];
        dst->tasks_by_type[i] += src->tasks_by_type[i];
    }
    for(i = 0u; i < RENDER_AUDIT_UNIT_COUNT; ++i) {
        dst->unit_cycles[i] += src->unit_cycles[i];
        dst->unit_count[i] += src->unit_count[i];
        dst->evaluate_calls[i] += src->evaluate_calls[i];
        dst->evaluate_accept[i] += src->evaluate_accept[i];
        dst->evaluate_reject[i] += src->evaluate_reject[i];
    }
    for(i = 0u; i < RENDER_AUDIT_REJECT_BUCKETS; ++i) {
        uint32_t mask = src->reject_mask[i];
        if(mask == 0u) continue;
        uint32_t j;
        for(j = 0u; j < RENDER_AUDIT_REJECT_BUCKETS; ++j) {
            if(dst->reject_mask[j] == 0u || dst->reject_mask[j] == mask) {
                dst->reject_mask[j] = mask;
                dst->reject_count[j] += src->reject_count[i];
                dst->reject_pixels[j] += src->reject_pixels[i];
                break;
            }
        }
    }
    for(i = 0u; i < RENDER_AUDIT_DMA_MODE_COUNT; ++i) {
        dst->dma_mode_cycles[i] += src->dma_mode_cycles[i];
        dst->dma_mode_count[i] += src->dma_mode_count[i];
        dst->dma_mode_pixels[i] += src->dma_mode_pixels[i];
    }
    for(i = 0u; i < RENDER_AUDIT_DMA_KIND_COUNT; ++i) {
        dst->dma_kind_active_cycles[i] += src->dma_kind_active_cycles[i];
        dst->dma_kind_wait_cycles[i] += src->dma_kind_wait_cycles[i];
        dst->dma_kind_setup_cycles[i] += src->dma_kind_setup_cycles[i];
        dst->dma_kind_count[i] += src->dma_kind_count[i];
        dst->dma_kind_pixels[i] += src->dma_kind_pixels[i];
    }
    for(i = 0u; i < RENDER_AUDIT_DMA_DEP_COUNT; ++i) {
        dst->dma_dependency_count[i] += src->dma_dependency_count[i];
        dst->dma_dependency_pixels[i] += src->dma_dependency_pixels[i];
        dst->dma_dependency_wait_cycles[i] += src->dma_dependency_wait_cycles[i];
    }
#define ADD_FIELD(name) dst->name += src->name
    ADD_FIELD(objects_considered); ADD_FIELD(objects_hidden);
    ADD_FIELD(objects_clip_rejected); ADD_FIELD(objects_drawn);
    ADD_FIELD(traversal_calls); ADD_FIELD(cover_check_calls);
    ADD_FIELD(cover_hit); ADD_FIELD(cover_miss); ADD_FIELD(style_get_calls);
    ADD_FIELD(tasks_created); ADD_FIELD(alloc_calls); ADD_FIELD(alloc_bytes);
    ADD_FIELD(free_calls); ADD_FIELD(dispatch_calls); ADD_FIELD(queue_scan_entries);
    ADD_FIELD(blocked_tasks); ADD_FIELD(cache_calls); ADD_FIELD(cache_bytes);
    ADD_FIELD(preclear_area_count); ADD_FIELD(preclear_pixels); ADD_FIELD(preclear_bytes);
    ADD_FIELD(preclear_opaque_pixels); ADD_FIELD(preclear_cleared_pixels);
    ADD_FIELD(preclear_skipped_pixels); ADD_FIELD(preclear_dma_error_count);
    ADD_FIELD(dma_hard_wait_cycles); ADD_FIELD(dma_hideable_wait_cycles);
    g_render_audit_total.preclear_dma_error_flags |= s_frame.preclear_dma_error_flags;
    ADD_FIELD(rect_count); ADD_FIELD(rect_overflow); ADD_FIELD(stack_error_count);
#undef ADD_FIELD
    if(src->queue_depth_max > dst->queue_depth_max) dst->queue_depth_max = src->queue_depth_max;
}

void RenderAudit_Reset(void)
{
    memset((void *)&g_render_audit_last, 0, sizeof(g_render_audit_last));
    memset((void *)&g_render_audit_total, 0, sizeof(g_render_audit_total));
    memset((void *)g_render_audit_rects, 0, sizeof(g_render_audit_rects));
    memset((void *)g_render_audit_preclear_rects, 0, sizeof(g_render_audit_preclear_rects));
    memset(&s_frame, 0, sizeof(s_frame));
    g_render_audit_frame_count = 0u;
    g_render_audit_merge_calls = 0u;
    g_render_audit_merge_cycles = 0u;
    g_render_audit_merge_comparisons = 0u;
    g_render_audit_invalid_before = 0u;
    g_render_audit_invalid_after = 0u;
    g_render_audit_invalidate_calls = 0u;
    g_render_audit_inv_p_peak = 0u;
    g_render_audit_inv_overflow_count = 0u;
    g_render_audit_dirty_frame_count = 0u;
    g_render_audit_dirty_frame_index = 0u;
    memset((void *)g_render_audit_dirty_frames, 0, sizeof(g_render_audit_dirty_frames));
    g_render_audit_layout_calls = 0u;
    g_render_audit_layout_passes = 0u;
    g_render_audit_layout_cycles = 0u;
    /* The registered object pointers stay valid across a trace reset; only the
     * per-frame and cumulative draw counters are cleared.  The registry itself
     * is rebuilt by RenderAudit_ObjectRegistryReset() on Launcher creation. */
    memset((void *)g_render_audit_object_draw_last, 0,
           sizeof(g_render_audit_object_draw_last));
    memset((void *)g_render_audit_object_draw_total, 0,
           sizeof(g_render_audit_object_draw_total));
    s_frame_active = 0u;
    s_stack_depth = 0u;
    s_category_start = 0u;
    s_task_accounting = 0u;
    s_irq_active = 0u;
    s_swdraw_task_handle = NULL;
    s_dma_kind_hint = RENDER_AUDIT_DMA_KIND_OTHER;
    s_dma_kind = RENDER_AUDIT_DMA_KIND_OTHER;
    s_dma_wait_start = 0u;
    s_dma_setup_start = 0u;
    s_dma_dependency_pending = 0u;
    s_dma_pending_wait_cycles = 0u;
    s_dirty_pending = 0u;
    s_dirty_pending_index = 0u;
}

void RenderAudit_SetEnabled(uint32_t enabled)
{
    g_render_audit_enabled = enabled != 0u;
}

void RenderAudit_FrameBegin(uint32_t frame_seq)
{
    uint32_t now;
    if(g_render_audit_enabled == 0u) return;
    memset(&s_frame, 0, sizeof(s_frame));
    memset((void *)g_render_audit_rects, 0, sizeof(g_render_audit_rects));
    memset((void *)g_render_audit_preclear_rects, 0, sizeof(g_render_audit_preclear_rects));
    memset((void *)g_render_audit_object_draw_last, 0, sizeof(g_render_audit_object_draw_last));
    s_frame.frame_seq = frame_seq;
    s_stack_depth = 1u;
    s_stack[0] = RENDER_AUDIT_CAT_BOOKKEEPING;
    s_task_class = classify_current_task();
    s_irq_active = 0u;
    now = now_cycles();
    s_frame_start = now;
    s_category_start = now;
    s_task_start = now;
    s_task_accounting = 1u;
    s_frame_active = 1u;
    /* LV_EVENT_RENDER_START is emitted after lv_refr_join_area(), so the dirty
     * record for this frame already exists.  Attach the render cycles to it. */
    if(g_render_audit_dirty_frame_count != 0u) {
        s_dirty_pending_index = (g_render_audit_dirty_frame_index +
                                 RENDER_AUDIT_DIRTY_FRAME_CAPACITY - 1u) %
                                RENDER_AUDIT_DIRTY_FRAME_CAPACITY;
        s_dirty_pending = 1u;
    }
}

void RenderAudit_FrameEnd(void)
{
    uint32_t now;
    if(s_frame_active == 0u) return;
    now = now_cycles();
    stop_category(now);
    stop_task(now);
    if(s_dma_dependency_pending != 0u) {
        ++s_frame.dma_dependency_count[RENDER_AUDIT_DMA_DEP_UNKNOWN];
        s_frame.dma_dependency_wait_cycles[RENDER_AUDIT_DMA_DEP_UNKNOWN] += s_dma_pending_wait_cycles;
        s_frame.dma_hard_wait_cycles += s_dma_pending_wait_cycles;
        s_dma_dependency_pending = 0u;
    }
    s_frame.frame_cycles = now - s_frame_start;
    if(s_dirty_pending != 0u) {
        s_dirty_pending = 0u;
        g_render_audit_dirty_frames[s_dirty_pending_index].render_cycles = s_frame.frame_cycles;
    }
    memcpy((void *)&g_render_audit_last, &s_frame, sizeof(s_frame));
    add_frame(&g_render_audit_total, &s_frame);
    ++g_render_audit_frame_count;
    s_frame_active = 0u;
    s_stack_depth = 0u;
}

void RenderAudit_Begin(RenderAuditCategory category)
{
    uint32_t now;
    if(s_frame_active == 0u || category >= RENDER_AUDIT_CAT_COUNT) return;
    now = now_cycles();
    stop_category(now);
    if(s_stack_depth >= RENDER_AUDIT_STACK_DEPTH) {
        ++s_frame.stack_error_count;
        start_category(now);
        return;
    }
    s_stack[s_stack_depth++] = category;
    start_category(now);
}

void RenderAudit_End(RenderAuditCategory category)
{
    uint32_t now;
    if(s_frame_active == 0u || category >= RENDER_AUDIT_CAT_COUNT) return;
    now = now_cycles();
    stop_category(now);
    if(s_stack_depth <= 1u || s_stack[s_stack_depth - 1u] != category) {
        ++s_frame.stack_error_count;
        start_category(now);
        return;
    }
    --s_stack_depth;
    start_category(now);
}

void RenderAudit_TaskSwitchedOut(void)
{
    uint32_t now;
    if(s_frame_active == 0u || s_irq_active != 0u) return;
    now = now_cycles();
    stop_category(now);
    stop_task(now);
}

void RenderAudit_TaskSwitchedIn(void)
{
    uint32_t now;
    if(s_frame_active == 0u || s_irq_active != 0u) return;
    now = now_cycles();
    s_task_class = classify_current_task();
    start_task(now);
    start_category(now);
}

void RenderAudit_IrqBegin(RenderAuditIrq irq)
{
    uint32_t now;
    (void)irq;
    if(s_frame_active == 0u || s_irq_active != 0u) return;
    now = now_cycles();
    stop_category(now);
    stop_task(now);
    s_irq_start = now;
    s_irq_active = 1u;
}

void RenderAudit_IrqEnd(RenderAuditIrq irq)
{
    uint32_t now;
    if(s_frame_active == 0u || s_irq_active == 0u || irq >= RENDER_AUDIT_IRQ_COUNT) return;
    now = now_cycles();
    s_frame.irq_cycles[irq] += now - s_irq_start;
    ++s_frame.irq_count[irq];
    s_irq_active = 0u;
    s_task_class = classify_current_task();
    start_task(now);
    start_category(now);
}

void RenderAudit_ObjectConsidered(void) { if(s_frame_active) ++s_frame.objects_considered; }
void RenderAudit_ObjectHidden(void) { if(s_frame_active) ++s_frame.objects_hidden; }
void RenderAudit_ObjectClipRejected(void) { if(s_frame_active) ++s_frame.objects_clip_rejected; }
void RenderAudit_ObjectDrawn(void) { if(s_frame_active) ++s_frame.objects_drawn; }
void RenderAudit_TraversalCall(void) { if(s_frame_active) ++s_frame.traversal_calls; }
void RenderAudit_StyleGet(void) { if(s_frame_active) ++s_frame.style_get_calls; }

void RenderAudit_CoverResult(uint32_t hit)
{
    if(s_frame_active == 0u) return;
    ++s_frame.cover_check_calls;
    if(hit) ++s_frame.cover_hit; else ++s_frame.cover_miss;
}

void RenderAudit_TaskCreated(uint32_t type, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2)
{
    if(s_frame_active == 0u) return;
    ++s_frame.tasks_created;
    if(type < RENDER_AUDIT_DRAW_TYPE_COUNT) ++s_frame.tasks_by_type[type];
    if(s_frame.rect_count < RENDER_AUDIT_RECT_CAPACITY) {
        volatile RenderAuditRect *r = &g_render_audit_rects[s_frame.rect_count++];
        r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
        r->x2 = (int16_t)x2; r->y2 = (int16_t)y2;
        r->type = (uint8_t)type;
    }
    else ++s_frame.rect_overflow;
}

void RenderAudit_TaskCoverage(uint32_t type, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, RenderAuditCoverage coverage)
{
    if(s_frame_active == 0u || coverage == RENDER_AUDIT_COVERAGE_UNKNOWN) return;
    uint32_t limit = s_frame.rect_count < RENDER_AUDIT_RECT_CAPACITY ?
                     s_frame.rect_count : RENDER_AUDIT_RECT_CAPACITY;
    for(uint32_t i = limit; i > 0u; --i) {
        volatile RenderAuditRect *r = &g_render_audit_rects[i - 1u];
        if(r->reserved[0] == RENDER_AUDIT_COVERAGE_UNKNOWN && r->type == type &&
           r->x1 == x1 && r->y1 == y1 && r->x2 == x2 && r->y2 == y2) {
            r->reserved[0] = (uint8_t)coverage;
            return;
        }
    }
}

void RenderAudit_PreclearArea(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                              uint32_t bytes_per_pixel, uint32_t cleared,
                              uint32_t opaque_cover)
{
    if(s_frame_active == 0u || x2 < x1 || y2 < y1) return;
    uint32_t pixels = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);
    uint32_t index = s_frame.preclear_area_count++;
    s_frame.preclear_pixels += pixels;
    s_frame.preclear_bytes += pixels * bytes_per_pixel;
    if(opaque_cover) s_frame.preclear_opaque_pixels += pixels;
    if(cleared) s_frame.preclear_cleared_pixels += pixels;
    else s_frame.preclear_skipped_pixels += pixels;
    if(index < RENDER_AUDIT_PRECLEAR_CAPACITY) {
        volatile RenderAuditPreclearRect *r = &g_render_audit_preclear_rects[index];
        r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
        r->x2 = (int16_t)x2; r->y2 = (int16_t)y2;
        r->cleared = cleared != 0u;
        r->opaque_cover = opaque_cover != 0u;
    }
}

void RenderAudit_PreclearDmaStatus(uint32_t error_flags)
{
    if(s_frame_active == 0u || error_flags == 0u) return;
    ++s_frame.preclear_dma_error_count;
    s_frame.preclear_dma_error_flags |= error_flags;
}

void RenderAudit_Alloc(size_t bytes)
{
    if(s_frame_active) { ++s_frame.alloc_calls; s_frame.alloc_bytes += (uint32_t)bytes; }
}

void RenderAudit_Free(void) { if(s_frame_active) ++s_frame.free_calls; }

void RenderAudit_Evaluate(uint32_t unit, uint32_t accepted)
{
    if(s_frame_active == 0u || unit >= RENDER_AUDIT_UNIT_COUNT) return;
    ++s_frame.evaluate_calls[unit];
    if(accepted) ++s_frame.evaluate_accept[unit]; else ++s_frame.evaluate_reject[unit];
}

void RenderAudit_DmaReject(uint32_t mask, uint32_t pixels)
{
    uint32_t i;
    if(s_frame_active == 0u || mask == 0u) return;
    for(i = 0u; i < RENDER_AUDIT_REJECT_BUCKETS; ++i) {
        if(s_frame.reject_mask[i] == 0u || s_frame.reject_mask[i] == mask) {
            s_frame.reject_mask[i] = mask;
            ++s_frame.reject_count[i];
            s_frame.reject_pixels[i] += pixels;
            return;
        }
    }
}

void RenderAudit_Dispatch(uint32_t queue_depth)
{
    if(s_frame_active == 0u) return;
    ++s_frame.dispatch_calls;
    if(queue_depth > s_frame.queue_depth_max) s_frame.queue_depth_max = queue_depth;
}

void RenderAudit_QueueScan(uint32_t blocked)
{
    if(s_frame_active == 0u) return;
    ++s_frame.queue_scan_entries;
    if(blocked) ++s_frame.blocked_tasks;
}

void RenderAudit_DrawExecBegin(uint32_t type, uint32_t unit, int32_t x1, int32_t y1,
                               int32_t x2, int32_t y2)
{
    uint32_t now;
    if(s_frame_active == 0u || unit >= RENDER_AUDIT_UNIT_COUNT) return;
    /* The CMSIS-RTOS2 OSAL drops LVGL's "swdraw" name.  The worker entry is
     * therefore the first unambiguous point at which to classify that task. */
    if(unit == RENDER_AUDIT_UNIT_SW && s_task_class == RENDER_AUDIT_TASK_OTHER && s_irq_active == 0u) {
        now = now_cycles();
        stop_category(now);
        stop_task(now);
        s_swdraw_task_handle = xTaskGetCurrentTaskHandle();
        s_task_class = RENDER_AUDIT_TASK_SWDRAW;
        start_task(now);
        start_category(now);
    }
    s_draw_type = (uint8_t)type;
    s_draw_unit = (uint8_t)unit;
    s_draw_start[unit] = now_cycles();
    s_draw_rect[unit].x1 = (int16_t)x1;
    s_draw_rect[unit].y1 = (int16_t)y1;
    s_draw_rect[unit].x2 = (int16_t)x2;
    s_draw_rect[unit].y2 = (int16_t)y2;
    RenderAudit_Begin(unit == RENDER_AUDIT_UNIT_SW ? RENDER_AUDIT_CAT_SW_EXECUTE :
                      RENDER_AUDIT_CAT_DMA2D_SETUP);
}

void RenderAudit_DrawExecEnd(uint32_t type, uint32_t unit)
{
    uint32_t now;
    if(s_frame_active == 0u || unit >= RENDER_AUDIT_UNIT_COUNT) return;
    now = now_cycles();
    if(type < RENDER_AUDIT_DRAW_TYPE_COUNT) {
        s_frame.draw_type_cycles[type] += now - s_draw_start[unit];
        ++s_frame.draw_type_count[type];
    }
    if(unit < RENDER_AUDIT_UNIT_COUNT) {
        s_frame.unit_cycles[unit] += now - s_draw_start[unit];
        ++s_frame.unit_count[unit];
        uint32_t limit = s_frame.rect_count < RENDER_AUDIT_RECT_CAPACITY ?
                         s_frame.rect_count : RENDER_AUDIT_RECT_CAPACITY;
        for(uint32_t i = 0u; i < limit; ++i) {
            volatile RenderAuditRect *rect = &g_render_audit_rects[i];
            if(rect->exec_cycles == 0u && rect->type == type &&
               rect->x1 == s_draw_rect[unit].x1 && rect->y1 == s_draw_rect[unit].y1 &&
               rect->x2 == s_draw_rect[unit].x2 && rect->y2 == s_draw_rect[unit].y2) {
                rect->unit = (uint8_t)unit;
                rect->exec_cycles = now - s_draw_start[unit];
                break;
            }
        }
    }
    RenderAudit_End(unit == RENDER_AUDIT_UNIT_SW ? RENDER_AUDIT_CAT_SW_EXECUTE :
                    RENDER_AUDIT_CAT_DMA2D_SETUP);
    (void)s_draw_type; (void)s_draw_unit;
}

void RenderAudit_DmaTransferBegin(uint32_t mode, uint32_t pixels)
{
    if(s_frame_active == 0u || mode >= RENDER_AUDIT_DMA_MODE_COUNT) return;
    s_dma_start = now_cycles();
    s_dma_mode = (uint8_t)mode;
    s_dma_kind = s_dma_kind_hint < RENDER_AUDIT_DMA_KIND_COUNT ?
                 s_dma_kind_hint : RENDER_AUDIT_DMA_KIND_OTHER;
    s_dma_kind_hint = RENDER_AUDIT_DMA_KIND_OTHER;
    s_dma_region = s_dma_region_candidate;
    s_dma_dependency_pending = 1u;
    s_dma_pending_wait_cycles = 0u;
    ++s_frame.dma_mode_count[mode];
    s_frame.dma_mode_pixels[mode] += pixels;
    ++s_frame.dma_kind_count[s_dma_kind];
    s_frame.dma_kind_pixels[s_dma_kind] += pixels;
}

void RenderAudit_DmaTransferEnd(uint32_t mode)
{
    (void)mode;
    if(s_frame_active == 0u || s_dma_mode >= RENDER_AUDIT_DMA_MODE_COUNT) return;
    uint32_t elapsed = now_cycles() - s_dma_start;
    s_frame.dma_mode_cycles[s_dma_mode] += elapsed;
    if(s_dma_kind < RENDER_AUDIT_DMA_KIND_COUNT) {
        s_frame.dma_kind_active_cycles[s_dma_kind] += elapsed;
    }
}

void RenderAudit_DmaJobHint(RenderAuditDmaKind kind)
{
    if(kind < RENDER_AUDIT_DMA_KIND_COUNT) s_dma_kind_hint = (uint8_t)kind;
}

void RenderAudit_DmaWaitBegin(void)
{
    if(s_frame_active != 0u) s_dma_wait_start = now_cycles();
}

void RenderAudit_DmaWaitEnd(void)
{
    if(s_frame_active == 0u || s_dma_wait_start == 0u ||
       s_dma_kind >= RENDER_AUDIT_DMA_KIND_COUNT) return;
    uint32_t elapsed = now_cycles() - s_dma_wait_start;
    s_frame.dma_kind_wait_cycles[s_dma_kind] += elapsed;
    s_dma_pending_wait_cycles += elapsed;
    s_dma_wait_start = 0u;
}

void RenderAudit_DmaSetupBegin(void)
{
    if(s_frame_active == 0u) return;
    s_dma_setup_kind = s_dma_kind_hint < RENDER_AUDIT_DMA_KIND_COUNT ?
                       s_dma_kind_hint : RENDER_AUDIT_DMA_KIND_OTHER;
    s_dma_setup_start = now_cycles();
}

void RenderAudit_DmaSetupEnd(void)
{
    if(s_frame_active == 0u || s_dma_setup_start == 0u ||
       s_dma_setup_kind >= RENDER_AUDIT_DMA_KIND_COUNT) return;
    s_frame.dma_kind_setup_cycles[s_dma_setup_kind] += now_cycles() - s_dma_setup_start;
    s_dma_setup_start = 0u;
}

void RenderAudit_DmaRegion(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    s_dma_region_candidate.x1 = (int16_t)x1;
    s_dma_region_candidate.y1 = (int16_t)y1;
    s_dma_region_candidate.x2 = (int16_t)x2;
    s_dma_region_candidate.y2 = (int16_t)y2;
}

void RenderAudit_DrawDependencyRegion(uint32_t unit, int32_t x1, int32_t y1,
                                      int32_t x2, int32_t y2, uint32_t reads_destination)
{
    if(s_frame_active == 0u) return;
    if(s_dma_dependency_pending != 0u) {
        int32_t ix1 = x1 > s_dma_region.x1 ? x1 : s_dma_region.x1;
        int32_t iy1 = y1 > s_dma_region.y1 ? y1 : s_dma_region.y1;
        int32_t ix2 = x2 < s_dma_region.x2 ? x2 : s_dma_region.x2;
        int32_t iy2 = y2 < s_dma_region.y2 ? y2 : s_dma_region.y2;
        if(ix1 > ix2 || iy1 > iy2) {
            ++s_frame.dma_dependency_count[RENDER_AUDIT_DMA_DEP_INDEPENDENT];
            s_frame.dma_dependency_wait_cycles[RENDER_AUDIT_DMA_DEP_INDEPENDENT] += s_dma_pending_wait_cycles;
            s_frame.dma_hideable_wait_cycles += s_dma_pending_wait_cycles;
        }
        else {
            uint32_t pixels = (uint32_t)(ix2 - ix1 + 1) * (uint32_t)(iy2 - iy1 + 1);
            if(reads_destination != 0u) {
                ++s_frame.dma_dependency_count[RENDER_AUDIT_DMA_DEP_RAW];
                s_frame.dma_dependency_pixels[RENDER_AUDIT_DMA_DEP_RAW] += pixels;
                s_frame.dma_dependency_wait_cycles[RENDER_AUDIT_DMA_DEP_RAW] += s_dma_pending_wait_cycles;
            }
            ++s_frame.dma_dependency_count[RENDER_AUDIT_DMA_DEP_WAW];
            s_frame.dma_dependency_pixels[RENDER_AUDIT_DMA_DEP_WAW] += pixels;
            s_frame.dma_dependency_wait_cycles[RENDER_AUDIT_DMA_DEP_WAW] += s_dma_pending_wait_cycles;
            s_frame.dma_hard_wait_cycles += s_dma_pending_wait_cycles;
        }
        s_dma_dependency_pending = 0u;
    }
    if(unit == RENDER_AUDIT_UNIT_DMA2D) RenderAudit_DmaRegion(x1, y1, x2, y2);
}

void RenderAudit_Cache(uint32_t bytes)
{
    if(s_frame_active) { ++s_frame.cache_calls; s_frame.cache_bytes += bytes; }
}

uint32_t RenderAudit_MeasureBegin(void)
{
    return g_render_audit_enabled ? now_cycles() : 0u;
}

void RenderAudit_MergeEnd(uint32_t start, uint32_t comparisons)
{
    if(g_render_audit_enabled == 0u || start == 0u) return;
    ++g_render_audit_merge_calls;
    g_render_audit_merge_cycles += now_cycles() - start;
    g_render_audit_merge_comparisons += comparisons;
}

void RenderAudit_InvalidAreaCounts(uint32_t before, uint32_t after)
{
    if(g_render_audit_enabled == 0u) return;
    g_render_audit_invalid_before += before;
    g_render_audit_invalid_after += after;
}

void RenderAudit_InvalidAreaAppend(void)
{
    if(g_render_audit_enabled != 0u) ++g_render_audit_invalidate_calls;
}

void RenderAudit_InvalidAreaOverflow(void)
{
    if(g_render_audit_enabled != 0u) ++g_render_audit_inv_overflow_count;
}

static uint32_t dirty_area_pixels(const RenderAuditArea * area)
{
    int32_t w = (int32_t)area->x2 - (int32_t)area->x1 + 1;
    int32_t h = (int32_t)area->y2 - (int32_t)area->y1 + 1;
    if(w <= 0 || h <= 0) return 0u;
    return (uint32_t)w * (uint32_t)h;
}

/* Copy one {x1,y1,x2,y2} group out of a caller-owned lv_area_t array without
 * relying on struct aliasing.  `areas` is a flat int32_t stream. */
static void dirty_read_area(const void * areas, uint32_t index, RenderAuditArea * out)
{
    int32_t v[4];
    memcpy(v, (const uint8_t *)areas + (size_t)index * 4u * sizeof(int32_t), sizeof(v));
    out->x1 = (int16_t)v[0];
    out->y1 = (int16_t)v[1];
    out->x2 = (int16_t)v[2];
    out->y2 = (int16_t)v[3];
}

void RenderAudit_InvalidAreasPreJoin(const void * areas, uint32_t count)
{
    uint32_t i;
    uint32_t stored;
    if(g_render_audit_enabled == 0u) return;

    volatile RenderAuditDirtyFrame * frame =
        &g_render_audit_dirty_frames[g_render_audit_dirty_frame_index];
    memset((void *)frame, 0, sizeof(*frame));
    frame->ordinal = g_render_audit_dirty_frame_count;
    frame->frame_seq = s_frame.frame_seq;
    frame->pre_join_count = count;
    if(count > RENDER_AUDIT_INV_AREA_CAPACITY) {
        frame->truncated = 1u;
        count = RENDER_AUDIT_INV_AREA_CAPACITY;
    }
    for(i = 0u; i < count; ++i) {
        RenderAuditArea area;
        dirty_read_area(areas, i, &area);
        stored = i;
        frame->pre_join[stored] = area;
        frame->pre_join_pixels += dirty_area_pixels(&area);
    }
    if(g_render_audit_inv_p_peak < frame->pre_join_count) {
        g_render_audit_inv_p_peak = frame->pre_join_count;
    }
    s_dirty_pending_index = g_render_audit_dirty_frame_index;
    s_dirty_pending = 1u;
}

void RenderAudit_InvalidAreasPostJoin(const void * areas, const uint8_t * joined,
                                      uint32_t count)
{
    uint32_t i;
    uint32_t out = 0u;
    if(g_render_audit_enabled == 0u) return;

    volatile RenderAuditDirtyFrame * frame =
        &g_render_audit_dirty_frames[g_render_audit_dirty_frame_index];
    for(i = 0u; i < count; ++i) {
        RenderAuditArea area;
        if(joined[i] != 0u) continue;
        if(out >= RENDER_AUDIT_INV_AREA_CAPACITY) {
            frame->truncated = 1u;
            break;
        }
        dirty_read_area(areas, i, &area);
        frame->joined[out] = area;
        frame->joined_pixels += dirty_area_pixels(&area);
        ++out;
    }
    frame->joined_count = out;
    g_render_audit_dirty_frame_index =
        (g_render_audit_dirty_frame_index + 1u) % RENDER_AUDIT_DIRTY_FRAME_CAPACITY;
    ++g_render_audit_dirty_frame_count;
}

uint32_t RenderAudit_LayoutBegin(void)
{
    return g_render_audit_enabled != 0u ? now_cycles() : 0u;
}

void RenderAudit_LayoutEnd(uint32_t start, uint32_t passes)
{
    if(g_render_audit_enabled == 0u || start == 0u) return;
    ++g_render_audit_layout_calls;
    g_render_audit_layout_passes += passes;
    g_render_audit_layout_cycles += now_cycles() - start;
}

void RenderAudit_ObjectRegistryReset(void)
{
    g_render_audit_object_count = 0u;
    memset((void *)g_render_audit_object_ptr, 0, sizeof(g_render_audit_object_ptr));
    memset((void *)g_render_audit_object_kind, 0, sizeof(g_render_audit_object_kind));
    memset((void *)g_render_audit_object_index, 0, sizeof(g_render_audit_object_index));
    memset((void *)g_render_audit_object_draw_last, 0,
           sizeof(g_render_audit_object_draw_last));
    memset((void *)g_render_audit_object_draw_total, 0,
           sizeof(g_render_audit_object_draw_total));
}

void RenderAudit_RegisterObject(const void * obj, uint32_t kind, uint32_t index)
{
    uint32_t slot;
    if(obj == NULL || kind == RENDER_AUDIT_OBJ_KIND_UNKNOWN) return;
    if(kind >= RENDER_AUDIT_OBJ_KIND_COUNT) return;
    slot = g_render_audit_object_count;
    if(slot >= RENDER_AUDIT_OBJECT_CAPACITY) return;
    g_render_audit_object_ptr[slot] = (uintptr_t)obj;
    g_render_audit_object_kind[slot] = (uint8_t)kind;
    g_render_audit_object_index[slot] = (uint8_t)index;
    g_render_audit_object_count = slot + 1u;
}

void RenderAudit_ObjectDrawnPtr(const void * obj)
{
    uint32_t i;
    uintptr_t target;
    if(s_frame_active == 0u || obj == NULL) return;
    target = (uintptr_t)obj;
    for(i = 0u; i < g_render_audit_object_count; ++i) {
        if(g_render_audit_object_ptr[i] == target) {
            ++g_render_audit_object_draw_last[i];
            ++g_render_audit_object_draw_total[i];
            return;
        }
    }
}

#endif
