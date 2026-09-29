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
static uint8_t s_dma_mode;
static uint8_t s_frame_active;
static uint8_t s_task_accounting;
static uint8_t s_irq_active;
static uint8_t s_draw_type;
static uint8_t s_draw_unit;
static RenderAuditTaskClass s_task_class;
static TaskHandle_t s_swdraw_task_handle;

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
    s_frame_active = 0u;
    s_stack_depth = 0u;
    s_category_start = 0u;
    s_task_accounting = 0u;
    s_irq_active = 0u;
    s_swdraw_task_handle = NULL;
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
}

void RenderAudit_FrameEnd(void)
{
    uint32_t now;
    if(s_frame_active == 0u) return;
    now = now_cycles();
    stop_category(now);
    stop_task(now);
    s_frame.frame_cycles = now - s_frame_start;
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
    ++s_frame.dma_mode_count[mode];
    s_frame.dma_mode_pixels[mode] += pixels;
}

void RenderAudit_DmaTransferEnd(uint32_t mode)
{
    (void)mode;
    if(s_frame_active == 0u || s_dma_mode >= RENDER_AUDIT_DMA_MODE_COUNT) return;
    s_frame.dma_mode_cycles[s_dma_mode] += now_cycles() - s_dma_start;
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

#endif
