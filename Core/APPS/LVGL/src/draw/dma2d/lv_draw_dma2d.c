/**
 * @file lv_draw_dma2d.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_dma2d_private.h"
#include "render_audit.h"
#if LV_USE_DRAW_DMA2D

#include "../sw/lv_draw_sw.h"
#include "../../misc/lv_area_private.h"
#include "../lv_draw_buf_private.h"

#if defined(__ZEPHYR__) && LV_USE_DRAW_DMA2D_INTERRUPT
    #include <zephyr/kernel.h>
    #include <zephyr/irq.h>
#endif

#if !LV_DRAW_DMA2D_ASYNC && LV_USE_DRAW_DMA2D_INTERRUPT
    #warning LV_USE_DRAW_DMA2D_INTERRUPT is 1 but has no effect because LV_USE_OS is LV_OS_NONE
#endif

/*********************
 *      DEFINES
 *********************/

#define DRAW_UNIT_ID_DMA2D 5

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int32_t evaluate_cb(lv_draw_unit_t * draw_unit, lv_draw_task_t * task);
static int32_t dispatch_cb(lv_draw_unit_t * draw_unit, lv_layer_t * layer);
static int32_t delete_cb(lv_draw_unit_t * draw_unit);
static void dma2d_buf_clear_cb(lv_draw_buf_t * draw_buf, const lv_area_t * area, lv_layer_t * layer);
#if LV_DRAW_DMA2D_ASYNC
    static int32_t wait_finish_cb(lv_draw_unit_t * u);
#endif
static void post_transfer_tasks(lv_draw_dma2d_unit_t * u);
#if defined(__ZEPHYR__) && LV_USE_DRAW_DMA2D_INTERRUPT
    static void zephyr_dma2d_irq_handler(void *);
#endif
#if LV_DRAW_DMA2D_CACHE
    static void invalidate_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area);
    static void flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/

#if LV_DRAW_DMA2D_ASYNC
    static lv_draw_dma2d_unit_t * g_unit;
#endif

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_draw_buf_dma2d_init_handlers(void)
{
    lv_draw_buf_handlers_t * handlers = lv_draw_buf_get_handlers();
    handlers->buf_clear_cb = dma2d_buf_clear_cb;
#if LV_DRAW_DMA2D_CACHE
    lv_draw_buf_handlers_t * font_handlers = lv_draw_buf_get_font_handlers();
    lv_draw_buf_handlers_t * image_handlers = lv_draw_buf_get_image_handlers();
    handlers->invalidate_cache_cb = invalidate_cache;
    handlers->flush_cache_cb = flush_cache;
    font_handlers->invalidate_cache_cb = invalidate_cache;
    font_handlers->flush_cache_cb = flush_cache;
    image_handlers->invalidate_cache_cb = invalidate_cache;
    image_handlers->flush_cache_cb = flush_cache;
#endif
}

void lv_draw_dma2d_init(void)
{
    lv_draw_buf_dma2d_init_handlers();
    lv_draw_dma2d_unit_t * draw_dma2d_unit = lv_draw_create_unit(sizeof(lv_draw_dma2d_unit_t));
    draw_dma2d_unit->base_unit.evaluate_cb = evaluate_cb;
    draw_dma2d_unit->base_unit.dispatch_cb = dispatch_cb;
    draw_dma2d_unit->base_unit.delete_cb = delete_cb;
#if LV_DRAW_DMA2D_ASYNC
    draw_dma2d_unit->base_unit.wait_for_finish_cb = wait_finish_cb;
#endif
    draw_dma2d_unit->base_unit.name = "DMA2D";

#if LV_DRAW_DMA2D_ASYNC
    g_unit = draw_dma2d_unit;
    lv_thread_sync_init(&draw_dma2d_unit->interrupt_signal);
#endif

    /* enable the DMA2D clock */
#if defined(STM32F4) || defined(STM32F7) || defined(STM32U5) || defined(STM32L4)
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2DEN;
#if defined(STM32F4) || defined(STM32F7)
    RCC->AHB1LPENR |= RCC_AHB1LPENR_DMA2DLPEN;
#endif
#elif defined(STM32H7)
    RCC->AHB3ENR |= RCC_AHB3ENR_DMA2DEN;
    RCC->AHB3LPENR |= RCC_AHB3LPENR_DMA2DLPEN;
#elif defined(STM32H7RS) || defined(STM32N6)
    RCC->AHB5ENR |= RCC_AHB5ENR_DMA2DEN;
    RCC->AHB5LPENR |= RCC_AHB5LPENR_DMA2DLPEN;
#else
#warning "LVGL can't enable the clock for DMA2D"
#endif

    /* disable dead time */
    DMA2D->AMTCR = 0;

#if defined(__ZEPHYR__) && LV_USE_DRAW_DMA2D_INTERRUPT
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(dma2d)), DT_IRQ(DT_NODELABEL(dma2d), priority), zephyr_dma2d_irq_handler, NULL, 0);
    irq_enable(DT_IRQN(DT_NODELABEL(dma2d)));
#else
    /* enable the interrupt */
    NVIC_EnableIRQ(DMA2D_IRQn);
#endif
}

void lv_draw_dma2d_deinit(void)
{
    /* disable the interrupt */
    NVIC_DisableIRQ(DMA2D_IRQn);

    /* disable the DMA2D clock */
#if defined(STM32F4) || defined(STM32F7) || defined(STM32U5) || defined(STM32L4)
    RCC->AHB1ENR &= ~RCC_AHB1ENR_DMA2DEN;
#elif defined(STM32H7)
    RCC->AHB3ENR &= ~RCC_AHB3ENR_DMA2DEN;
#elif defined(STM32H7RS) || defined(STM32N6)
    RCC->AHB5ENR &= ~RCC_AHB5ENR_DMA2DEN;
#endif

#if LV_DRAW_DMA2D_ASYNC
    lv_result_t res = lv_thread_sync_delete(&g_unit->interrupt_signal);
    LV_ASSERT(res == LV_RESULT_OK);

    g_unit = NULL;
#endif
}

#if LV_USE_DRAW_DMA2D_INTERRUPT
void lv_draw_dma2d_transfer_complete_interrupt_handler(void)
{
#if LV_DRAW_DMA2D_ASYNC
    lv_thread_sync_signal_isr(&g_unit->interrupt_signal);
#endif
}
#endif

lv_draw_dma2d_output_cf_t lv_draw_dma2d_cf_to_dma2d_output_cf(lv_color_format_t cf)
{
    switch(cf) {
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_XRGB8888:
            return LV_DRAW_DMA2D_OUTPUT_CF_ARGB8888;
        case LV_COLOR_FORMAT_RGB888:
            return LV_DRAW_DMA2D_OUTPUT_CF_RGB888;
        case LV_COLOR_FORMAT_RGB565:
            return LV_DRAW_DMA2D_OUTPUT_CF_RGB565;
        case LV_COLOR_FORMAT_ARGB1555:
            return LV_DRAW_DMA2D_OUTPUT_CF_ARGB1555;
        default:
            LV_ASSERT_MSG(false, "unsupported output color format");
    }
    return LV_DRAW_DMA2D_OUTPUT_CF_RGB565;
}

uint32_t lv_draw_dma2d_color_to_dma2d_color(lv_draw_dma2d_output_cf_t cf, lv_color_t color)
{
    switch(cf) {
        case LV_DRAW_DMA2D_OUTPUT_CF_ARGB8888:
        case LV_DRAW_DMA2D_OUTPUT_CF_RGB888:
            return lv_color_to_u32(color);
        case LV_DRAW_DMA2D_OUTPUT_CF_RGB565:
            return lv_color_to_u16(color);
        default:
            LV_ASSERT_MSG(false, "unsupported output color format");
    }
    return 0;
}

void lv_draw_dma2d_configure_and_start_transfer(const lv_draw_dma2d_configuration_t * conf)
{
    /* Check that addresses are valid regarding to alignment constraints */
    if(((conf->output_cf == LV_DRAW_DMA2D_OUTPUT_CF_ARGB8888) &&
        (((uint32_t)(lv_uintptr_t) conf->output_address) & 0x03)) ||
       ((conf->output_cf == LV_DRAW_DMA2D_OUTPUT_CF_RGB888) &&
        (((uint32_t)(lv_uintptr_t) conf->output_address) & 0x03)) ||
       ((conf->output_cf == LV_DRAW_DMA2D_OUTPUT_CF_RGB565) &&
        (((uint32_t)(lv_uintptr_t) conf->output_address) & 0x01)) ||
       ((conf->output_cf == LV_DRAW_DMA2D_OUTPUT_CF_ARGB1555) &&
        (((uint32_t)(lv_uintptr_t) conf->output_address) & 0x01))) {
        LV_LOG_WARN("Incompatible output address %p and format 0x%x",
                    conf->output_address, conf->output_cf);
    }
    if(((conf->fg_cf == LV_DRAW_DMA2D_FGBG_CF_ARGB8888) &&
        (((uint32_t)(lv_uintptr_t) conf->fg_address) & 0x03)) ||
       ((conf->fg_cf == LV_DRAW_DMA2D_FGBG_CF_RGB888) &&
        (((uint32_t)(lv_uintptr_t) conf->fg_address) & 0x03)) ||
       ((conf->fg_cf == LV_DRAW_DMA2D_FGBG_CF_RGB565) &&
        (((uint32_t)(lv_uintptr_t) conf->fg_address) & 0x01)) ||
       ((conf->fg_cf == LV_DRAW_DMA2D_FGBG_CF_ARGB1555) &&
        (((uint32_t)(lv_uintptr_t) conf->fg_address) & 0x01))) {
        LV_LOG_WARN("Incompatible foreground address %p and format 0x%x",
                    conf->fg_address, conf->fg_cf);
    }
    if(((conf->bg_cf == LV_DRAW_DMA2D_FGBG_CF_ARGB8888) &&
        (((uint32_t)(lv_uintptr_t) conf->bg_address) & 0x03)) ||
       ((conf->bg_cf == LV_DRAW_DMA2D_FGBG_CF_RGB888) &&
        (((uint32_t)(lv_uintptr_t) conf->bg_address) & 0x03)) ||
       ((conf->bg_cf == LV_DRAW_DMA2D_FGBG_CF_RGB565) &&
        (((uint32_t)(lv_uintptr_t) conf->bg_address) & 0x01)) ||
       ((conf->bg_cf == LV_DRAW_DMA2D_FGBG_CF_ARGB1555) &&
        (((uint32_t)(lv_uintptr_t) conf->bg_address) & 0x01))) {
        LV_LOG_WARN("Incompatible background address %p and format 0x%x",
                    conf->bg_address, conf->bg_cf);
    }

    /* number of lines register */
    DMA2D->NLR = (conf->w << DMA2D_NLR_PL_Pos) | (conf->h << DMA2D_NLR_NL_Pos);

    /* output */

    /* output memory address register */
    DMA2D->OMAR = (uint32_t)(uintptr_t) conf->output_address;
    /* output offset register */
    DMA2D->OOR = conf->output_offset;
    /* output pixel format converter control register */
    DMA2D->OPFCCR = ((uint32_t) conf->output_cf) << DMA2D_OPFCCR_CM_Pos;

    /* Fill color. Only for mode LV_DRAW_DMA2D_MODE_REGISTER_TO_MEMORY */
    DMA2D->OCOLR = conf->reg_to_mem_mode_color;

    /* foreground */

    /* foreground memory address register */
    DMA2D->FGMAR = (uint32_t)(uintptr_t) conf->fg_address;
    /* foreground offset register */
    DMA2D->FGOR = conf->fg_offset;
    /* foreground color. only for mem-to-mem with blending and fixed-color foreground */
    DMA2D->FGCOLR = conf->fg_color;
    /* foreground pixel format converter control register */
    DMA2D->FGPFCCR = (((uint32_t) conf->fg_cf) << DMA2D_FGPFCCR_CM_Pos)
                     | (conf->fg_alpha << DMA2D_FGPFCCR_ALPHA_Pos)
                     | (conf->fg_alpha_mode << DMA2D_FGPFCCR_AM_Pos);

    /* background */

    DMA2D->BGMAR = (uint32_t)(uintptr_t) conf->bg_address;
    DMA2D->BGOR = conf->bg_offset;
    DMA2D->BGCOLR = conf->bg_color;
    DMA2D->BGPFCCR = (((uint32_t) conf->bg_cf) << DMA2D_BGPFCCR_CM_Pos)
                     | (conf->bg_alpha << DMA2D_BGPFCCR_ALPHA_Pos)
                     | (conf->bg_alpha_mode << DMA2D_BGPFCCR_AM_Pos);

    /* ensure the DMA2D register values are observed before the start transfer bit is set */
    __DSB();

    /* start the transfer (also set mode and enable transfer complete interrupt) */
    DMA2D->CR = DMA2D_CR_START | (((uint32_t) conf->mode) << DMA2D_CR_MODE_Pos)
#if LV_USE_DRAW_DMA2D_INTERRUPT
                | DMA2D_CR_TCIE
#endif
                ;
    uint32_t audit_mode = conf->mode == LV_DRAW_DMA2D_MODE_REGISTER_TO_MEMORY ? 0u :
                          (conf->mode == LV_DRAW_DMA2D_MODE_MEMORY_TO_MEMORY_WITH_BLENDING ||
                           conf->mode == LV_DRAW_DMA2D_MODE_MEMORY_TO_MEMORY_WITH_BLENDING_AND_FIXED_COLOR_FG ||
                           conf->mode == LV_DRAW_DMA2D_MODE_MEMORY_TO_MEMORY_WITH_BLENDING_AND_FIXED_COLOR_BG) ? 2u : 1u;
    RenderAudit_DmaTransferBegin(audit_mode, conf->w * conf->h);
}


#if LV_DRAW_DMA2D_CACHE
static void __invalidate_flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area,
                                     bool flush)
{
    LV_ASSERT(draw_buf != NULL);
    LV_ASSERT(area != NULL);

    const lv_image_header_t * header = &draw_buf->header;
    uint32_t stride = header->stride;
    lv_color_format_t cf = header->cf;

    uint32_t bpp = lv_color_format_get_bpp(cf);
    int32_t lines = lv_area_get_height(area);

    if(lines <= 0 || bpp == 0U || stride == 0U) {
        return;
    }

    /* As area coordinates, x1, x2 and y1 are always expected to be values > 0 */
    LV_ASSERT(area->x1 >= 0);
    LV_ASSERT(area->x2 >= 0);
    LV_ASSERT(area->y1 >= 0);
    LV_ASSERT(area->x2 >= area->x1);

    uint64_t start_bit = (uint64_t)(uint32_t)area->x1 * (uint64_t)bpp;
    uint64_t end_bit = (uint64_t)((uint32_t)area->x2 + 1U) * (uint64_t)bpp;
    uint32_t start_byte = (uint32_t)(start_bit >> 3);
    uint32_t end_byte = (uint32_t)((end_bit + 7U) >> 3);
    int32_t bytes_to_flush_per_line = (int32_t)(end_byte - start_byte);
    uint8_t * address = draw_buf->data + start_byte + (stride * (uint32_t)area->y1);
    int32_t i = 0;

    if(bytes_to_flush_per_line <= 0) {
        return;
    }

    for(i = 0; i < lines; i++) {
        if(SCB->CCR & SCB_CCR_DC_Msk) {
            if(flush) {
                SCB_CleanDCache_by_Addr(address, bytes_to_flush_per_line);
            }
            else {
                SCB_InvalidateDCache_by_Addr(address, bytes_to_flush_per_line);
            }
        }
        address += stride;
    }
}

static void invalidate_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    __invalidate_flush_cache(draw_buf, area, false);
}

static void flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    __invalidate_flush_cache(draw_buf, area, true);
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void cpu_buf_clear(lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    uint32_t bpp = lv_color_format_get_bpp(draw_buf->header.cf);
    uint32_t line_bytes = ((uint32_t)lv_area_get_width(area) * bpp + 7u) >> 3;
    uint8_t * dst = lv_draw_buf_goto_xy(draw_buf, area->x1, area->y1);
    for(int32_t y = area->y1; y <= area->y2; ++y) {
        lv_memzero(dst, line_bytes);
        dst += draw_buf->header.stride;
    }
    lv_draw_buf_flush_cache(draw_buf, area);
}

static void dma2d_buf_clear_cb(lv_draw_buf_t * draw_buf, const lv_area_t * area, lv_layer_t * layer)
{
    LV_UNUSED(layer);
    lv_area_t full = {
        .x1 = 0,
        .y1 = 0,
        .x2 = (int32_t)draw_buf->header.w - 1,
        .y2 = (int32_t)draw_buf->header.h - 1,
    };
    lv_area_t clipped;
    if(area == NULL) area = &full;
    if(!lv_area_intersect(&clipped, area, &full)) return;

#if CARTDESK_RENDER_AUDIT_ENABLE
    if(g_render_audit_preclear_mode != RENDER_AUDIT_PRECLEAR_DMA2D) {
        cpu_buf_clear(draw_buf, &clipped);
        return;
    }
#endif

    lv_color_format_t cf = draw_buf->header.cf;
    uint32_t bpp = lv_color_format_get_bpp(cf);
    if(cf != LV_COLOR_FORMAT_ARGB8888 || bpp == 0u || (bpp & 7u) != 0u) {
        cpu_buf_clear(draw_buf, &clipped);
        return;
    }

    uint32_t bytes_per_pixel = bpp >> 3;
    uint32_t stride_pixels = draw_buf->header.stride / bytes_per_pixel;
    uint32_t width = (uint32_t)lv_area_get_width(&clipped);
    uint32_t height = (uint32_t)lv_area_get_height(&clipped);
    lv_draw_buf_flush_cache(draw_buf, &clipped);

    /* Draw-buffer clears run before this layer's draw tasks are dispatched.
     * Keep the existing synchronous ownership model and wait for any prior
     * transfer before programming the shared DMA2D registers. */
    while(DMA2D->CR & DMA2D_CR_START) {}
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    lv_draw_dma2d_configuration_t conf = {
        .mode = LV_DRAW_DMA2D_MODE_REGISTER_TO_MEMORY,
        .w = width,
        .h = height,
        .output_address = lv_draw_buf_goto_xy(draw_buf, clipped.x1, clipped.y1),
        .output_offset = stride_pixels - width,
        .output_cf = lv_draw_dma2d_cf_to_dma2d_output_cf(cf),
        .reg_to_mem_mode_color = 0u,
    };
    lv_draw_dma2d_configure_and_start_transfer(&conf);
    while(DMA2D->CR & DMA2D_CR_START) {}
    RenderAudit_PreclearDmaStatus(DMA2D->ISR & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF));
    RenderAudit_DmaTransferEnd(0u);
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    lv_draw_buf_invalidate_cache(draw_buf, &clipped);
}

#if defined(__ZEPHYR__) && LV_USE_DRAW_DMA2D_INTERRUPT
static void zephyr_dma2d_irq_handler(void *)
{
    /* Clear Transfer Complete flag */
    DMA2D->IFCR = DMA2D_IFCR_CTCIF;

    lv_draw_dma2d_transfer_complete_interrupt_handler();
}
#endif

static int32_t evaluate_cb(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)
{
    uint32_t audit_reject = 0u;
    switch(task->type) {
        case LV_DRAW_TASK_TYPE_FILL: {
                lv_draw_fill_dsc_t * dsc = task->draw_dsc;
                if(dsc->radius != 0) audit_reject |= 0x002u;
                if(dsc->grad.dir != LV_GRAD_DIR_NONE) audit_reject |= 0x004u;
                if(!(dsc->base.layer->color_format == LV_COLOR_FORMAT_ARGB8888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_XRGB8888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_RGB888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_RGB565)) audit_reject |= 0x008u;
            }
            break;
        case LV_DRAW_TASK_TYPE_IMAGE: {
                lv_draw_image_dsc_t * dsc = task->draw_dsc;
                if(dsc->header.cf >= LV_COLOR_FORMAT_PROPRIETARY_START) audit_reject |= 0x200u;
                if(dsc->clip_radius != 0) audit_reject |= 0x002u;
                if(dsc->bitmap_mask_src != NULL || dsc->sup != NULL || dsc->tile != 0) audit_reject |= 0x010u;
                if(dsc->blend_mode != LV_BLEND_MODE_NORMAL) audit_reject |= 0x040u;
                if(dsc->recolor_opa > LV_OPA_MIN) audit_reject |= 0x020u;
                if(dsc->skew_y != 0 || dsc->skew_x != 0 || dsc->scale_x != LV_SCALE_NONE ||
                   dsc->scale_y != LV_SCALE_NONE || dsc->rotation != 0) audit_reject |= 0x080u;
                if(lv_image_src_get_type(dsc->src) != LV_IMAGE_SRC_VARIABLE) audit_reject |= 0x100u;
                if(!(dsc->header.cf == LV_COLOR_FORMAT_ARGB8888
                     || dsc->header.cf == LV_COLOR_FORMAT_XRGB8888
                     || dsc->header.cf == LV_COLOR_FORMAT_RGB888
                     || dsc->header.cf == LV_COLOR_FORMAT_RGB565
                     || dsc->header.cf == LV_COLOR_FORMAT_ARGB1555)) audit_reject |= 0x200u;
                if(!(dsc->base.layer->color_format == LV_COLOR_FORMAT_ARGB8888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_XRGB8888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_RGB888
                     || dsc->base.layer->color_format == LV_COLOR_FORMAT_RGB565)) audit_reject |= 0x008u;
            }
            break;
        default:
            audit_reject |= 0x001u;
            break;
    }

    if(audit_reject != 0u) {
        RenderAudit_DmaReject(audit_reject, (uint32_t)lv_area_get_size(&task->area));
        return 0;
    }

    task->preferred_draw_unit_id = DRAW_UNIT_ID_DMA2D;
    task->preference_score = 0;

    return 0;
}

static int32_t dispatch_cb(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    lv_draw_dma2d_unit_t * draw_dma2d_unit = (lv_draw_dma2d_unit_t *) draw_unit;

    if(draw_dma2d_unit->task_act) {
        /*Return immediately if it's busy with draw task*/
        return LV_DRAW_UNIT_IDLE;
    }

    lv_draw_task_t * t = lv_draw_get_available_task(layer, NULL, DRAW_UNIT_ID_DMA2D);
    if(t == NULL) {
        return LV_DRAW_UNIT_IDLE;
    }

    void * buf = lv_draw_layer_alloc_buf(layer);
    if(buf == NULL) {
        t->state = LV_DRAW_TASK_STATE_FAILED;
        return LV_DRAW_UNIT_IDLE;
    }

    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    t->draw_unit = draw_unit;
    draw_dma2d_unit->task_act = t;
    /* Abort rapidly if nothing to do */
    lv_area_t clipped_coords;
    if(!lv_area_intersect(&clipped_coords, &t->area, &t->clip_area)) {
        draw_dma2d_unit->task_act->state = LV_DRAW_TASK_STATE_FINISHED;
        draw_dma2d_unit->task_act = NULL;

        lv_draw_dispatch_request();
        return 1;
    }
    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t * dsc = t->draw_dsc;
        RenderAuditCoverage coverage =
            dsc->opa >= LV_OPA_MAX && t->opa >= LV_OPA_MAX && dsc->radius == 0 &&
            dsc->grad.dir == LV_GRAD_DIR_NONE ?
            RENDER_AUDIT_COVERAGE_OPAQUE : RENDER_AUDIT_COVERAGE_BLEND;
        RenderAudit_TaskCoverage((uint32_t)t->type, clipped_coords.x1, clipped_coords.y1,
                                 clipped_coords.x2, clipped_coords.y2, coverage);
    }
    RenderAudit_DrawExecBegin((uint32_t)t->type, RENDER_AUDIT_UNIT_DMA2D,
                              clipped_coords.x1, clipped_coords.y1,
                              clipped_coords.x2, clipped_coords.y2);

    int32_t x = 0 - t->target_layer->buf_area.x1;
    int32_t y = 0 - t->target_layer->buf_area.y1;

    draw_dma2d_unit->last_clipped_area = clipped_coords;
    lv_area_move(&draw_dma2d_unit->last_clipped_area, x, y);

    /* Flush cache before drawing. This is a no-op when DMA2D_CACHE is disabled */
    uint32_t audit_cache_bytes = (uint32_t)lv_area_get_width(&draw_dma2d_unit->last_clipped_area) *
                                 (uint32_t)lv_area_get_height(&draw_dma2d_unit->last_clipped_area) *
                                 (uint32_t)lv_color_format_get_bpp(layer->draw_buf->header.cf) / 8u;
    RenderAudit_Begin(RENDER_AUDIT_CAT_CACHE);
    lv_draw_buf_flush_cache(layer->draw_buf, &draw_dma2d_unit->last_clipped_area);
    RenderAudit_Cache(audit_cache_bytes);
    RenderAudit_End(RENDER_AUDIT_CAT_CACHE);

    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t * dsc = t->draw_dsc;

        void * dest = lv_draw_layer_go_to_xy(layer, draw_dma2d_unit->last_clipped_area.x1,
                                             draw_dma2d_unit->last_clipped_area.y1);

        lv_draw_dma2d_fill(t, dest,
                           lv_area_get_width(&clipped_coords),
                           lv_area_get_height(&clipped_coords),
                           lv_draw_buf_width_to_stride(lv_area_get_width(&layer->buf_area),
                                                       dsc->base.layer->color_format));
    }
    else if(t->type == LV_DRAW_TASK_TYPE_IMAGE) {
        lv_draw_dma2d_image(t, t->draw_dsc, &t->area);
    }

#if LV_DRAW_DMA2D_ASYNC
    return LV_DRAW_UNIT_IDLE;
#else
    RenderAudit_Begin(RENDER_AUDIT_CAT_DMA2D_WAIT);
    while(DMA2D->CR & DMA2D_CR_START);
    RenderAudit_End(RENDER_AUDIT_CAT_DMA2D_WAIT);
    RenderAudit_DmaTransferEnd(0u);

    RenderAudit_Begin(RENDER_AUDIT_CAT_CACHE);
    post_transfer_tasks(draw_dma2d_unit);
    RenderAudit_Cache(audit_cache_bytes);
    RenderAudit_End(RENDER_AUDIT_CAT_CACHE);
    RenderAudit_DrawExecEnd((uint32_t)t->type, RENDER_AUDIT_UNIT_DMA2D);

    lv_draw_dispatch_request();

    return 1;
#endif
}

static int32_t delete_cb(lv_draw_unit_t * draw_unit)
{
    return 0;
}

#if LV_DRAW_DMA2D_ASYNC
static int32_t wait_finish_cb(lv_draw_unit_t * draw_unit)
{
    lv_draw_dma2d_unit_t * u = (lv_draw_dma2d_unit_t *) draw_unit;

    /* No need to wait if the DMA2D doesn't have task to complete */
    if(u->task_act == NULL) return 0;

    /* If a DMA2D task has been dispatched, wait its interrupt */
    lv_thread_sync_wait(&u->interrupt_signal);

    /* Then cleanup the DMA2D draw unit to accept a new task */
    post_transfer_tasks(u);
    return 0;
}
#endif /*LV_DRAW_DMA2D_ASYNC*/

static void post_transfer_tasks(lv_draw_dma2d_unit_t * u)
{
    /* Invalidate cache after drawing. This is a no-op when DMA2D_CACHE is disabled */
    lv_draw_buf_invalidate_cache(u->task_act->target_layer->draw_buf, &u->last_clipped_area);

    u->task_act->state = LV_DRAW_TASK_STATE_FINISHED;
    u->task_act = NULL;
}

#endif /*LV_USE_DRAW_DMA2D*/
