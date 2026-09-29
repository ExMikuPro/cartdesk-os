#ifndef CARTDESK_RENDER_AUDIT_H
#define CARTDESK_RENDER_AUDIT_H

#include <stddef.h>
#include <stdint.h>

#ifndef CARTDESK_RENDER_AUDIT_ENABLE
#define CARTDESK_RENDER_AUDIT_ENABLE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RENDER_AUDIT_CAT_BOOKKEEPING = 0,
    RENDER_AUDIT_CAT_TRAVERSAL,
    RENDER_AUDIT_CAT_COVER,
    RENDER_AUDIT_CAT_TASK_CREATE,
    RENDER_AUDIT_CAT_DSC_INIT,
    RENDER_AUDIT_CAT_STYLE,
    RENDER_AUDIT_CAT_EVALUATE,
    RENDER_AUDIT_CAT_DISPATCH,
    RENDER_AUDIT_CAT_DMA2D_SETUP,
    RENDER_AUDIT_CAT_DMA2D_WAIT,
    RENDER_AUDIT_CAT_SW_EXECUTE,
    RENDER_AUDIT_CAT_DRAWBUF_CLEAR,
    RENDER_AUDIT_CAT_CACHE,
    RENDER_AUDIT_CAT_ALLOC,
    RENDER_AUDIT_CAT_CLEANUP,
    RENDER_AUDIT_CAT_COUNT
} RenderAuditCategory;

typedef enum {
    RENDER_AUDIT_TASK_APP = 0,
    RENDER_AUDIT_TASK_SWDRAW,
    RENDER_AUDIT_TASK_AUDIO,
    RENDER_AUDIT_TASK_IO,
    RENDER_AUDIT_TASK_BACKGROUND,
    RENDER_AUDIT_TASK_IDLE,
    RENDER_AUDIT_TASK_OTHER,
    RENDER_AUDIT_TASK_COUNT
} RenderAuditTaskClass;

typedef enum {
    RENDER_AUDIT_UNIT_NONE = 0,
    RENDER_AUDIT_UNIT_DMA2D,
    RENDER_AUDIT_UNIT_SW,
    RENDER_AUDIT_UNIT_COUNT
} RenderAuditUnit;

typedef enum {
    RENDER_AUDIT_IRQ_SYSTICK = 0,
    RENDER_AUDIT_IRQ_EXTI3,
    RENDER_AUDIT_IRQ_SDMMC1,
    RENDER_AUDIT_IRQ_USB,
    RENDER_AUDIT_IRQ_LTDC,
    RENDER_AUDIT_IRQ_DMA2D,
    RENDER_AUDIT_IRQ_MDMA,
    RENDER_AUDIT_IRQ_COUNT
} RenderAuditIrq;

#define RENDER_AUDIT_DRAW_TYPE_COUNT 16u
#define RENDER_AUDIT_DMA_MODE_COUNT  3u
#define RENDER_AUDIT_RECT_CAPACITY   160u
#define RENDER_AUDIT_REJECT_BUCKETS  16u
#define RENDER_AUDIT_PRECLEAR_CAPACITY 16u

typedef enum {
    RENDER_AUDIT_PRECLEAR_CURRENT = 0,
    RENDER_AUDIT_PRECLEAR_SKIP,
    RENDER_AUDIT_PRECLEAR_SELECTIVE,
    RENDER_AUDIT_PRECLEAR_DMA2D
} RenderAuditPreclearMode;

typedef enum {
    RENDER_AUDIT_COVERAGE_UNKNOWN = 0,
    RENDER_AUDIT_COVERAGE_OPAQUE,
    RENDER_AUDIT_COVERAGE_BLEND
} RenderAuditCoverage;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    uint8_t type;
    uint8_t unit;
    uint8_t reserved[2];
    uint32_t exec_cycles;
} RenderAuditRect;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    uint8_t cleared;
    uint8_t opaque_cover;
    uint8_t reserved[2];
} RenderAuditPreclearRect;

typedef struct {
    uint32_t frame_seq;
    uint32_t frame_cycles;
    uint32_t category_cycles[RENDER_AUDIT_CAT_COUNT];
    uint32_t task_cycles[RENDER_AUDIT_TASK_COUNT];
    uint32_t irq_cycles[RENDER_AUDIT_IRQ_COUNT];
    uint32_t irq_count[RENDER_AUDIT_IRQ_COUNT];
    uint32_t draw_type_cycles[RENDER_AUDIT_DRAW_TYPE_COUNT];
    uint32_t draw_type_count[RENDER_AUDIT_DRAW_TYPE_COUNT];
    uint32_t unit_cycles[RENDER_AUDIT_UNIT_COUNT];
    uint32_t unit_count[RENDER_AUDIT_UNIT_COUNT];
    uint32_t objects_considered;
    uint32_t objects_hidden;
    uint32_t objects_clip_rejected;
    uint32_t objects_drawn;
    uint32_t traversal_calls;
    uint32_t cover_check_calls;
    uint32_t cover_hit;
    uint32_t cover_miss;
    uint32_t style_get_calls;
    uint32_t tasks_created;
    uint32_t tasks_by_type[RENDER_AUDIT_DRAW_TYPE_COUNT];
    uint32_t alloc_calls;
    uint32_t alloc_bytes;
    uint32_t free_calls;
    uint32_t evaluate_calls[RENDER_AUDIT_UNIT_COUNT];
    uint32_t evaluate_accept[RENDER_AUDIT_UNIT_COUNT];
    uint32_t evaluate_reject[RENDER_AUDIT_UNIT_COUNT];
    uint32_t reject_mask[RENDER_AUDIT_REJECT_BUCKETS];
    uint32_t reject_count[RENDER_AUDIT_REJECT_BUCKETS];
    uint32_t reject_pixels[RENDER_AUDIT_REJECT_BUCKETS];
    uint32_t dispatch_calls;
    uint32_t queue_scan_entries;
    uint32_t blocked_tasks;
    uint32_t queue_depth_max;
    uint32_t dma_mode_cycles[RENDER_AUDIT_DMA_MODE_COUNT];
    uint32_t dma_mode_count[RENDER_AUDIT_DMA_MODE_COUNT];
    uint32_t dma_mode_pixels[RENDER_AUDIT_DMA_MODE_COUNT];
    uint32_t cache_calls;
    uint32_t cache_bytes;
    uint32_t preclear_area_count;
    uint32_t preclear_pixels;
    uint32_t preclear_bytes;
    uint32_t preclear_opaque_pixels;
    uint32_t preclear_cleared_pixels;
    uint32_t preclear_skipped_pixels;
    uint32_t preclear_dma_error_count;
    uint32_t preclear_dma_error_flags;
    uint32_t rect_count;
    uint32_t rect_overflow;
    uint32_t stack_error_count;
} RenderAuditFrame;

#if CARTDESK_RENDER_AUDIT_ENABLE

extern volatile uint32_t g_render_audit_enabled;
extern volatile uint32_t g_render_audit_frame_count;
extern volatile RenderAuditFrame g_render_audit_last;
extern volatile RenderAuditFrame g_render_audit_total;
extern volatile RenderAuditRect g_render_audit_rects[RENDER_AUDIT_RECT_CAPACITY];
extern volatile RenderAuditPreclearRect g_render_audit_preclear_rects[RENDER_AUDIT_PRECLEAR_CAPACITY];
extern volatile uint32_t g_render_audit_preclear_mode;
extern volatile uint32_t g_render_audit_merge_calls;
extern volatile uint32_t g_render_audit_merge_cycles;
extern volatile uint32_t g_render_audit_merge_comparisons;
extern volatile uint32_t g_render_audit_invalid_before;
extern volatile uint32_t g_render_audit_invalid_after;

void RenderAudit_Reset(void);
void RenderAudit_SetEnabled(uint32_t enabled);
void RenderAudit_FrameBegin(uint32_t frame_seq);
void RenderAudit_FrameEnd(void);
void RenderAudit_Begin(RenderAuditCategory category);
void RenderAudit_End(RenderAuditCategory category);
void RenderAudit_TaskSwitchedIn(void);
void RenderAudit_TaskSwitchedOut(void);
void RenderAudit_IrqBegin(RenderAuditIrq irq);
void RenderAudit_IrqEnd(RenderAuditIrq irq);
void RenderAudit_ObjectConsidered(void);
void RenderAudit_ObjectHidden(void);
void RenderAudit_ObjectClipRejected(void);
void RenderAudit_ObjectDrawn(void);
void RenderAudit_TraversalCall(void);
void RenderAudit_CoverResult(uint32_t hit);
void RenderAudit_StyleGet(void);
void RenderAudit_TaskCreated(uint32_t type, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2);
void RenderAudit_TaskCoverage(uint32_t type, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, RenderAuditCoverage coverage);
void RenderAudit_PreclearArea(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                              uint32_t bytes_per_pixel, uint32_t cleared,
                              uint32_t opaque_cover);
void RenderAudit_PreclearDmaStatus(uint32_t error_flags);
void RenderAudit_Alloc(size_t bytes);
void RenderAudit_Free(void);
void RenderAudit_Evaluate(uint32_t unit, uint32_t accepted);
void RenderAudit_DmaReject(uint32_t mask, uint32_t pixels);
void RenderAudit_Dispatch(uint32_t queue_depth);
void RenderAudit_QueueScan(uint32_t blocked);
void RenderAudit_DrawExecBegin(uint32_t type, uint32_t unit, int32_t x1, int32_t y1,
                               int32_t x2, int32_t y2);
void RenderAudit_DrawExecEnd(uint32_t type, uint32_t unit);
void RenderAudit_DmaTransferBegin(uint32_t mode, uint32_t pixels);
void RenderAudit_DmaTransferEnd(uint32_t mode);
void RenderAudit_Cache(uint32_t bytes);
uint32_t RenderAudit_MeasureBegin(void);
void RenderAudit_MergeEnd(uint32_t start, uint32_t comparisons);
void RenderAudit_InvalidAreaCounts(uint32_t before, uint32_t after);

#else

static inline void RenderAudit_Reset(void) {}
static inline void RenderAudit_SetEnabled(uint32_t enabled) { (void)enabled; }
static inline void RenderAudit_FrameBegin(uint32_t frame_seq) { (void)frame_seq; }
static inline void RenderAudit_FrameEnd(void) {}
static inline void RenderAudit_Begin(RenderAuditCategory category) { (void)category; }
static inline void RenderAudit_End(RenderAuditCategory category) { (void)category; }
static inline void RenderAudit_TaskSwitchedIn(void) {}
static inline void RenderAudit_TaskSwitchedOut(void) {}
static inline void RenderAudit_IrqBegin(RenderAuditIrq irq) { (void)irq; }
static inline void RenderAudit_IrqEnd(RenderAuditIrq irq) { (void)irq; }
static inline void RenderAudit_ObjectConsidered(void) {}
static inline void RenderAudit_ObjectHidden(void) {}
static inline void RenderAudit_ObjectClipRejected(void) {}
static inline void RenderAudit_ObjectDrawn(void) {}
static inline void RenderAudit_TraversalCall(void) {}
static inline void RenderAudit_CoverResult(uint32_t hit) { (void)hit; }
static inline void RenderAudit_StyleGet(void) {}
static inline void RenderAudit_TaskCreated(uint32_t type, int32_t x1, int32_t y1,
                                            int32_t x2, int32_t y2)
{ (void)type; (void)x1; (void)y1; (void)x2; (void)y2; }
static inline void RenderAudit_TaskCoverage(uint32_t type, int32_t x1, int32_t y1,
                                             int32_t x2, int32_t y2,
                                             RenderAuditCoverage coverage)
{ (void)type; (void)x1; (void)y1; (void)x2; (void)y2; (void)coverage; }
static inline void RenderAudit_PreclearArea(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                             uint32_t bytes_per_pixel, uint32_t cleared,
                                             uint32_t opaque_cover)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)bytes_per_pixel; (void)cleared; (void)opaque_cover; }
static inline void RenderAudit_PreclearDmaStatus(uint32_t error_flags) { (void)error_flags; }
static inline void RenderAudit_Alloc(size_t bytes) { (void)bytes; }
static inline void RenderAudit_Free(void) {}
static inline void RenderAudit_Evaluate(uint32_t unit, uint32_t accepted)
{ (void)unit; (void)accepted; }
static inline void RenderAudit_DmaReject(uint32_t mask, uint32_t pixels)
{ (void)mask; (void)pixels; }
static inline void RenderAudit_Dispatch(uint32_t queue_depth) { (void)queue_depth; }
static inline void RenderAudit_QueueScan(uint32_t blocked) { (void)blocked; }
static inline void RenderAudit_DrawExecBegin(uint32_t type, uint32_t unit, int32_t x1, int32_t y1,
                                             int32_t x2, int32_t y2)
{ (void)type; (void)unit; (void)x1; (void)y1; (void)x2; (void)y2; }
static inline void RenderAudit_DrawExecEnd(uint32_t type, uint32_t unit)
{ (void)type; (void)unit; }
static inline void RenderAudit_DmaTransferBegin(uint32_t mode, uint32_t pixels)
{ (void)mode; (void)pixels; }
static inline void RenderAudit_DmaTransferEnd(uint32_t mode) { (void)mode; }
static inline void RenderAudit_Cache(uint32_t bytes) { (void)bytes; }
static inline uint32_t RenderAudit_MeasureBegin(void) { return 0u; }
static inline void RenderAudit_MergeEnd(uint32_t start, uint32_t comparisons)
{ (void)start; (void)comparisons; }
static inline void RenderAudit_InvalidAreaCounts(uint32_t before, uint32_t after)
{ (void)before; (void)after; }

#endif

#ifdef __cplusplus
}
#endif

#endif
