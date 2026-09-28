#include "dma2d_selftest.h"

#if XHGC_DMA2D_SELFTEST_ENABLE

#include <stdbool.h>
#include <stdint.h>

#include "dma2d.h"
#include "main.h"
#include "sdram.h"
#include "xhgc_dcache.h"

#define DMA2D_SELFTEST_FB_WIDTH 800u
#define DMA2D_SELFTEST_FB_HEIGHT 480u
#define DMA2D_SELFTEST_RECT_X 137u
#define DMA2D_SELFTEST_RECT_Y 83u
#define DMA2D_SELFTEST_RECT_WIDTH 200u
#define DMA2D_SELFTEST_RECT_HEIGHT 200u
#define DMA2D_SELFTEST_BPP 4u
#define DMA2D_SELFTEST_GUARD_WORDS 32u
#define DMA2D_SELFTEST_GUARD_A UINT32_C(0xA5A5A5A5)
#define DMA2D_SELFTEST_GUARD_B UINT32_C(0x5A5A5A5A)
#define DMA2D_SELFTEST_BACKGROUND UINT32_C(0xFF112233)
#define DMA2D_SELFTEST_FILL UINT32_C(0xFFFF0000)
#define DMA2D_SELFTEST_TIMEOUT_CYCLES UINT32_C(240000000)
#define DMA2D_SELFTEST_FB_PIXELS (DMA2D_SELFTEST_FB_WIDTH * DMA2D_SELFTEST_FB_HEIGHT)
#define DMA2D_SELFTEST_FB_BYTES (DMA2D_SELFTEST_FB_PIXELS * DMA2D_SELFTEST_BPP)
#define DMA2D_SELFTEST_TOTAL_BYTES (DMA2D_SELFTEST_FB_BYTES + (2u * DMA2D_SELFTEST_GUARD_WORDS * sizeof(uint32_t)))
#define DMA2D_SELFTEST_TIGHT_PIXELS (DMA2D_SELFTEST_RECT_WIDTH * DMA2D_SELFTEST_RECT_HEIGHT)
#define DMA2D_SELFTEST_TIGHT_BYTES (DMA2D_SELFTEST_TIGHT_PIXELS * DMA2D_SELFTEST_BPP)
#define DMA2D_SELFTEST_STRIDED_WIDTH 256u
#define DMA2D_SELFTEST_STRIDED_HEIGHT 256u
#define DMA2D_SELFTEST_STRIDED_PIXELS (DMA2D_SELFTEST_STRIDED_WIDTH * DMA2D_SELFTEST_STRIDED_HEIGHT)
#define DMA2D_SELFTEST_STRIDED_BYTES (DMA2D_SELFTEST_STRIDED_PIXELS * DMA2D_SELFTEST_BPP)

volatile uint32_t g_dma2d_test_command;
volatile uint32_t g_dma2d_test_state;
volatile uint32_t g_dma2d_test_stage;
volatile uint32_t g_dma2d_test_pass;
volatile uint32_t g_dma2d_test_fail;
volatile uint32_t g_dma2d_test_case;
volatile uint32_t g_dma2d_test_error_x;
volatile uint32_t g_dma2d_test_error_y;
volatile uint32_t g_dma2d_test_expected;
volatile uint32_t g_dma2d_test_actual;
volatile uint32_t g_dma2d_test_cycles;
volatile uint32_t g_dma2d_test_time_us;
volatile uint32_t g_dma2d_test_lom;
volatile uint32_t g_dma2d_test_width;
volatile uint32_t g_dma2d_test_height;
volatile uint32_t g_dma2d_test_source_address;
volatile uint32_t g_dma2d_test_destination_address;
volatile uint32_t g_dma2d_test_source_stride;
volatile uint32_t g_dma2d_test_destination_stride;
volatile uint32_t g_dma2d_test_operation;
volatile DMA2D_SelftestRegisterSnapshot g_dma2d_test_registers;

static uint32_t *s_allocation;
static uint32_t *s_framebuffer;
static uint32_t *s_tight_source;
static uint32_t *s_strided_source;

static void snapshot_registers(void)
{
    g_dma2d_test_registers.cr = DMA2D->CR;
    g_dma2d_test_registers.isr = DMA2D->ISR;
    g_dma2d_test_registers.fgmar = DMA2D->FGMAR;
    g_dma2d_test_registers.fgor = DMA2D->FGOR;
    g_dma2d_test_registers.bgmar = DMA2D->BGMAR;
    g_dma2d_test_registers.bgor = DMA2D->BGOR;
    g_dma2d_test_registers.fgpfccr = DMA2D->FGPFCCR;
    g_dma2d_test_registers.bgpfccr = DMA2D->BGPFCCR;
    g_dma2d_test_registers.omar = DMA2D->OMAR;
    g_dma2d_test_registers.oor = DMA2D->OOR;
    g_dma2d_test_registers.opfccr = DMA2D->OPFCCR;
    g_dma2d_test_registers.nlr = DMA2D->NLR;
}

static void fail(uint32_t x, uint32_t y, uint32_t expected, uint32_t actual)
{
    g_dma2d_test_error_x = x;
    g_dma2d_test_error_y = y;
    g_dma2d_test_expected = expected;
    g_dma2d_test_actual = actual;
    snapshot_registers();
    ++g_dma2d_test_fail;
    g_dma2d_test_state = DMA2D_SELFTEST_STATE_FAIL;
}

static bool check_guards(void)
{
    for (uint32_t i = 0u; i < DMA2D_SELFTEST_GUARD_WORDS; ++i) {
        if (s_allocation[i] != DMA2D_SELFTEST_GUARD_A) {
            fail(i, 0u, DMA2D_SELFTEST_GUARD_A, s_allocation[i]);
            return false;
        }
        if (s_framebuffer[DMA2D_SELFTEST_FB_PIXELS + i] != DMA2D_SELFTEST_GUARD_B) {
            fail(i, 1u, DMA2D_SELFTEST_GUARD_B,
                 s_framebuffer[DMA2D_SELFTEST_FB_PIXELS + i]);
            return false;
        }
    }
    return true;
}

static bool prepare_buffer(void)
{
    if (s_allocation == NULL) {
        /* DMA_POOL is never reset here, so other owners cannot be overwritten. */
        s_allocation = SDRAM_DmaPoolAlloc(DMA2D_SELFTEST_TOTAL_BYTES, 32u);
        if (s_allocation == NULL) {
            fail(UINT32_MAX, UINT32_MAX, DMA2D_SELFTEST_TOTAL_BYTES, 0u);
            return false;
        }
        s_framebuffer = s_allocation + DMA2D_SELFTEST_GUARD_WORDS;
    }
    if ((((uintptr_t)s_framebuffer) & 31u) != 0u ||
        !SDRAM_DmaPoolContains(s_allocation, DMA2D_SELFTEST_TOTAL_BYTES)) {
        fail(UINT32_MAX, UINT32_MAX, 32u, (uint32_t)((uintptr_t)s_framebuffer & 31u));
        return false;
    }
    for (uint32_t i = 0u; i < DMA2D_SELFTEST_GUARD_WORDS; ++i) {
        s_allocation[i] = DMA2D_SELFTEST_GUARD_A;
        s_framebuffer[DMA2D_SELFTEST_FB_PIXELS + i] = DMA2D_SELFTEST_GUARD_B;
    }
    for (uint32_t i = 0u; i < DMA2D_SELFTEST_FB_PIXELS; ++i) {
        s_framebuffer[i] = DMA2D_SELFTEST_BACKGROUND;
    }
    xhgc_dcache_clean_range(s_allocation, DMA2D_SELFTEST_TOTAL_BYTES);
    __DSB();
    return true;
}

static bool prepare_tight_source(void)
{
    if (s_tight_source == NULL) {
        s_tight_source = SDRAM_DmaPoolAlloc(DMA2D_SELFTEST_TIGHT_BYTES, 32u);
        if (s_tight_source == NULL) {
            fail(UINT32_MAX, UINT32_MAX, DMA2D_SELFTEST_TIGHT_BYTES, 0u);
            return false;
        }
    }
    for (uint32_t y = 0u; y < DMA2D_SELFTEST_RECT_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < DMA2D_SELFTEST_RECT_WIDTH; ++x) {
            s_tight_source[y * DMA2D_SELFTEST_RECT_WIDTH + x] =
                UINT32_C(0xFF000000) | ((x & 0xFFu) << 16) |
                ((y & 0xFFu) << 8) | ((x ^ y) & 0xFFu);
        }
    }
    xhgc_dcache_clean_range(s_tight_source, DMA2D_SELFTEST_TIGHT_BYTES);
    __DSB();
    return true;
}

static bool prepare_strided_source(void)
{
    if (s_strided_source == NULL) {
        s_strided_source = SDRAM_DmaPoolAlloc(DMA2D_SELFTEST_STRIDED_BYTES, 32u);
        if (s_strided_source == NULL) {
            fail(UINT32_MAX, UINT32_MAX, DMA2D_SELFTEST_STRIDED_BYTES, 0u);
            return false;
        }
    }
    for (uint32_t y = 0u; y < DMA2D_SELFTEST_STRIDED_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < DMA2D_SELFTEST_STRIDED_WIDTH; ++x) {
            s_strided_source[y * DMA2D_SELFTEST_STRIDED_WIDTH + x] =
                UINT32_C(0xFF000000) | ((x & 0xFFu) << 16) |
                ((y & 0xFFu) << 8) | ((x ^ y) & 0xFFu);
        }
    }
    xhgc_dcache_clean_range(s_strided_source, DMA2D_SELFTEST_STRIDED_BYTES);
    __DSB();
    return true;
}

static bool run_r2m(void)
{
    uint32_t wait_start = DWT->CYCCNT;
    uint32_t destination_offset = ((DMA2D_SELFTEST_RECT_Y * DMA2D_SELFTEST_FB_WIDTH) +
                                   DMA2D_SELFTEST_RECT_X) * DMA2D_SELFTEST_BPP;
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_DMA2D;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - wait_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    DMA2D->FGMAR = 0u;
    DMA2D->FGOR = 0u;
    DMA2D->BGMAR = 0u;
    DMA2D->BGOR = 0u;
    DMA2D->FGPFCCR = 0u;
    DMA2D->BGPFCCR = 0u;
    DMA2D->OMAR = (uint32_t)(uintptr_t)((uint8_t *)s_framebuffer + destination_offset);
    DMA2D->OOR = DMA2D_SELFTEST_FB_WIDTH - DMA2D_SELFTEST_RECT_WIDTH;
    DMA2D->OPFCCR = DMA2D_OUTPUT_ARGB8888;
    DMA2D->OCOLR = DMA2D_SELFTEST_FILL;
    DMA2D->NLR = (DMA2D_SELFTEST_RECT_WIDTH << DMA2D_NLR_PL_Pos) |
                 (DMA2D_SELFTEST_RECT_HEIGHT << DMA2D_NLR_NL_Pos);
    /* LOM is deliberately clear: all line offsets are pixels. */
    DMA2D->CR = DMA2D_R2M;
    g_dma2d_test_lom = (DMA2D->CR & DMA2D_CR_LOM) != 0u ? 1u : 0u;
    g_dma2d_test_destination_address = DMA2D->OMAR;
    __DSB();
    uint32_t transfer_start = DWT->CYCCNT;
    DMA2D->CR |= DMA2D_CR_START;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - transfer_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            g_dma2d_test_cycles = DWT->CYCCNT - transfer_start;
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    g_dma2d_test_cycles = DWT->CYCCNT - transfer_start;
    if (SystemCoreClock >= 1000000u) {
        g_dma2d_test_time_us = g_dma2d_test_cycles / (SystemCoreClock / 1000000u);
    }
    snapshot_registers();
    if ((g_dma2d_test_registers.isr & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)) != 0u) {
        fail(UINT32_MAX, UINT32_MAX, 0u, g_dma2d_test_registers.isr);
        return false;
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    xhgc_dcache_invalidate_range(s_allocation, DMA2D_SELFTEST_TOTAL_BYTES);
    __DSB();
    return true;
}

static bool compare_framebuffer(void)
{
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_COMPARE;
    for (uint32_t y = 0u; y < DMA2D_SELFTEST_FB_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < DMA2D_SELFTEST_FB_WIDTH; ++x) {
            bool inside = x >= DMA2D_SELFTEST_RECT_X &&
                          x < (DMA2D_SELFTEST_RECT_X + DMA2D_SELFTEST_RECT_WIDTH) &&
                          y >= DMA2D_SELFTEST_RECT_Y &&
                          y < (DMA2D_SELFTEST_RECT_Y + DMA2D_SELFTEST_RECT_HEIGHT);
            uint32_t expected = inside ? DMA2D_SELFTEST_FILL : DMA2D_SELFTEST_BACKGROUND;
            uint32_t actual = s_framebuffer[y * DMA2D_SELFTEST_FB_WIDTH + x];
            if (actual != expected) {
                fail(x, y, expected, actual);
                return false;
            }
        }
    }
    return true;
}

static bool run_m2m_tight_at(uint32_t origin_x, uint32_t origin_y)
{
    uint32_t wait_start = DWT->CYCCNT;
    uint32_t destination_offset =
        ((origin_y * DMA2D_SELFTEST_FB_WIDTH) + origin_x) * DMA2D_SELFTEST_BPP;
    if (!prepare_buffer()) return false;
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_DMA2D;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - wait_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    DMA2D->FGMAR = (uint32_t)(uintptr_t)s_tight_source;
    DMA2D->FGOR = 0u;
    DMA2D->FGPFCCR = DMA2D_INPUT_ARGB8888;
    DMA2D->BGMAR = 0u;
    DMA2D->BGOR = 0u;
    DMA2D->BGPFCCR = 0u;
    DMA2D->OMAR = (uint32_t)(uintptr_t)((uint8_t *)s_framebuffer + destination_offset);
    DMA2D->OOR = DMA2D_SELFTEST_FB_WIDTH - DMA2D_SELFTEST_RECT_WIDTH;
    DMA2D->OPFCCR = DMA2D_OUTPUT_ARGB8888;
    DMA2D->NLR = (DMA2D_SELFTEST_RECT_WIDTH << DMA2D_NLR_PL_Pos) |
                 (DMA2D_SELFTEST_RECT_HEIGHT << DMA2D_NLR_NL_Pos);
    DMA2D->CR = DMA2D_M2M;
    g_dma2d_test_lom = (DMA2D->CR & DMA2D_CR_LOM) != 0u ? 1u : 0u;
    g_dma2d_test_source_address = DMA2D->FGMAR;
    g_dma2d_test_destination_address = DMA2D->OMAR;
    g_dma2d_test_source_stride = DMA2D_SELFTEST_RECT_WIDTH;
    g_dma2d_test_destination_stride = DMA2D_SELFTEST_FB_WIDTH;
    __DSB();
    uint32_t transfer_start = DWT->CYCCNT;
    DMA2D->CR |= DMA2D_CR_START;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - transfer_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    g_dma2d_test_cycles = DWT->CYCCNT - transfer_start;
    g_dma2d_test_time_us = SystemCoreClock >= 1000000u ?
        g_dma2d_test_cycles / (SystemCoreClock / 1000000u) : 0u;
    snapshot_registers();
    if ((g_dma2d_test_registers.isr & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)) != 0u) {
        fail(UINT32_MAX, UINT32_MAX, 0u, g_dma2d_test_registers.isr);
        return false;
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    xhgc_dcache_invalidate_range(s_allocation, DMA2D_SELFTEST_TOTAL_BYTES);
    __DSB();
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_GUARD;
    if (!check_guards()) return false;
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_COMPARE;
    for (uint32_t y = 0u; y < DMA2D_SELFTEST_FB_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < DMA2D_SELFTEST_FB_WIDTH; ++x) {
            bool inside = x >= origin_x && x < origin_x + DMA2D_SELFTEST_RECT_WIDTH &&
                          y >= origin_y && y < origin_y + DMA2D_SELFTEST_RECT_HEIGHT;
            uint32_t expected = DMA2D_SELFTEST_BACKGROUND;
            if (inside) {
                uint32_t source_x = x - origin_x;
                uint32_t source_y = y - origin_y;
                expected = s_tight_source[source_y * DMA2D_SELFTEST_RECT_WIDTH + source_x];
            }
            uint32_t actual = s_framebuffer[y * DMA2D_SELFTEST_FB_WIDTH + x];
            if (actual != expected) {
                fail(x, y, expected, actual);
                return false;
            }
        }
    }
    return true;
}

static bool run_m2m_strided_at(uint32_t source_x, uint32_t source_y,
                               uint32_t destination_x, uint32_t destination_y)
{
    uint32_t wait_start = DWT->CYCCNT;
    uint32_t destination_offset =
        ((destination_y * DMA2D_SELFTEST_FB_WIDTH) + destination_x) * DMA2D_SELFTEST_BPP;
    uint32_t source_offset =
        ((source_y * DMA2D_SELFTEST_STRIDED_WIDTH) + source_x) * DMA2D_SELFTEST_BPP;

    if (source_x + DMA2D_SELFTEST_RECT_WIDTH > DMA2D_SELFTEST_STRIDED_WIDTH ||
        source_y + DMA2D_SELFTEST_RECT_HEIGHT > DMA2D_SELFTEST_STRIDED_HEIGHT ||
        destination_x + DMA2D_SELFTEST_RECT_WIDTH > DMA2D_SELFTEST_FB_WIDTH ||
        destination_y + DMA2D_SELFTEST_RECT_HEIGHT > DMA2D_SELFTEST_FB_HEIGHT) {
        fail(UINT32_MAX, UINT32_MAX, 0u, 0u);
        return false;
    }
    if (!prepare_buffer()) return false;
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_DMA2D;
    g_dma2d_test_source_address = (uint32_t)(uintptr_t)((uint8_t *)s_strided_source + source_offset);
    g_dma2d_test_destination_address = (uint32_t)(uintptr_t)((uint8_t *)s_framebuffer + destination_offset);
    g_dma2d_test_source_stride = DMA2D_SELFTEST_STRIDED_WIDTH;
    g_dma2d_test_destination_stride = DMA2D_SELFTEST_FB_WIDTH;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - wait_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    DMA2D->FGMAR = g_dma2d_test_source_address;
    DMA2D->FGOR = DMA2D_SELFTEST_STRIDED_WIDTH - DMA2D_SELFTEST_RECT_WIDTH;
    DMA2D->FGPFCCR = DMA2D_INPUT_ARGB8888;
    DMA2D->BGMAR = 0u;
    DMA2D->BGOR = 0u;
    DMA2D->BGPFCCR = 0u;
    DMA2D->OMAR = g_dma2d_test_destination_address;
    DMA2D->OOR = DMA2D_SELFTEST_FB_WIDTH - DMA2D_SELFTEST_RECT_WIDTH;
    DMA2D->OPFCCR = DMA2D_OUTPUT_ARGB8888;
    DMA2D->NLR = (DMA2D_SELFTEST_RECT_WIDTH << DMA2D_NLR_PL_Pos) |
                 (DMA2D_SELFTEST_RECT_HEIGHT << DMA2D_NLR_NL_Pos);
    /* LOM is deliberately clear: all line offsets are pixels. */
    DMA2D->CR = DMA2D_M2M;
    g_dma2d_test_lom = (DMA2D->CR & DMA2D_CR_LOM) != 0u ? 1u : 0u;
    __DSB();
    uint32_t transfer_start = DWT->CYCCNT;
    DMA2D->CR |= DMA2D_CR_START;
    while ((DMA2D->CR & DMA2D_CR_START) != 0u) {
        if ((DWT->CYCCNT - transfer_start) > DMA2D_SELFTEST_TIMEOUT_CYCLES) {
            g_dma2d_test_cycles = DWT->CYCCNT - transfer_start;
            fail(UINT32_MAX, UINT32_MAX, 0u, DMA2D->CR);
            return false;
        }
    }
    g_dma2d_test_cycles = DWT->CYCCNT - transfer_start;
    g_dma2d_test_time_us = SystemCoreClock >= 1000000u ?
        g_dma2d_test_cycles / (SystemCoreClock / 1000000u) : 0u;
    snapshot_registers();
    if ((g_dma2d_test_registers.isr & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)) != 0u) {
        fail(UINT32_MAX, UINT32_MAX, 0u, g_dma2d_test_registers.isr);
        return false;
    }
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                  DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
    xhgc_dcache_invalidate_range(s_allocation, DMA2D_SELFTEST_TOTAL_BYTES);
    __DSB();
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_GUARD;
    if (!check_guards()) return false;
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_COMPARE;
    for (uint32_t y = 0u; y < DMA2D_SELFTEST_FB_HEIGHT; ++y) {
        for (uint32_t x = 0u; x < DMA2D_SELFTEST_FB_WIDTH; ++x) {
            bool inside = x >= destination_x && x < destination_x + DMA2D_SELFTEST_RECT_WIDTH &&
                          y >= destination_y && y < destination_y + DMA2D_SELFTEST_RECT_HEIGHT;
            uint32_t expected = DMA2D_SELFTEST_BACKGROUND;
            if (inside) {
                uint32_t source_pixel_x = source_x + x - destination_x;
                uint32_t source_pixel_y = source_y + y - destination_y;
                expected = s_strided_source[source_pixel_y * DMA2D_SELFTEST_STRIDED_WIDTH +
                                            source_pixel_x];
            }
            uint32_t actual = s_framebuffer[y * DMA2D_SELFTEST_FB_WIDTH + x];
            if (actual != expected) {
                fail(x, y, expected, actual);
                return false;
            }
        }
    }
    return true;
}

void DMA2D_Selftest_Poll(void)
{
    uint32_t command = g_dma2d_test_command;
    if ((command != DMA2D_SELFTEST_COMMAND_R2M_FILL &&
         command != DMA2D_SELFTEST_COMMAND_M2M_TIGHT &&
         command != DMA2D_SELFTEST_COMMAND_M2M_STRIDED) ||
        g_dma2d_test_state == DMA2D_SELFTEST_STATE_RUNNING) return;
    g_dma2d_test_command = DMA2D_SELFTEST_COMMAND_NONE;
    g_dma2d_test_case = command;
    g_dma2d_test_state = DMA2D_SELFTEST_STATE_RUNNING;
    g_dma2d_test_pass = 0u;
    g_dma2d_test_fail = 0u;
    g_dma2d_test_error_x = UINT32_MAX;
    g_dma2d_test_error_y = UINT32_MAX;
    g_dma2d_test_expected = 0u;
    g_dma2d_test_actual = 0u;
    g_dma2d_test_cycles = 0u;
    g_dma2d_test_time_us = 0u;
    g_dma2d_test_lom = 0u;
    g_dma2d_test_width = DMA2D_SELFTEST_RECT_WIDTH;
    g_dma2d_test_height = DMA2D_SELFTEST_RECT_HEIGHT;
    g_dma2d_test_source_address = 0u;
    g_dma2d_test_destination_address = 0u;
    g_dma2d_test_source_stride = 0u;
    g_dma2d_test_destination_stride = DMA2D_SELFTEST_FB_WIDTH;
    g_dma2d_test_operation = command;
    if (command == DMA2D_SELFTEST_COMMAND_R2M_FILL) {
        g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_ALLOCATE;
        if (!prepare_buffer() || !run_r2m()) return;
        g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_GUARD;
        if (!check_guards() || !compare_framebuffer()) return;
    } else if (command == DMA2D_SELFTEST_COMMAND_M2M_TIGHT) {
        static const uint16_t x_positions[] = {0u, 1u, 7u, 31u, 32u, 137u, 599u};
        static const uint16_t y_positions[] = {0u, 1u, 37u, 279u};
        g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_ALLOCATE;
        if (!prepare_tight_source()) return;
        for (uint32_t yi = 0u; yi < sizeof(y_positions) / sizeof(y_positions[0]); ++yi) {
            for (uint32_t xi = 0u; xi < sizeof(x_positions) / sizeof(x_positions[0]); ++xi) {
                if (!run_m2m_tight_at(x_positions[xi], y_positions[yi])) return;
            }
        }
    } else {
        static const uint16_t source_x_positions[] = {0u, 1u, 7u, 17u, 31u, 32u, 55u};
        static const uint16_t source_y_positions[] = {0u, 1u, 23u, 37u};
        static const uint16_t destination_x_positions[] = {599u, 137u, 32u, 31u, 7u, 1u, 0u};
        static const uint16_t destination_y_positions[] = {279u, 37u, 1u, 0u};
        g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_ALLOCATE;
        if (!prepare_strided_source()) return;
        for (uint32_t yi = 0u; yi < sizeof(source_y_positions) / sizeof(source_y_positions[0]); ++yi) {
            for (uint32_t xi = 0u; xi < sizeof(source_x_positions) / sizeof(source_x_positions[0]); ++xi) {
                g_dma2d_test_case = yi * (sizeof(source_x_positions) / sizeof(source_x_positions[0])) + xi + 1u;
                if (!run_m2m_strided_at(source_x_positions[xi], source_y_positions[yi],
                                        destination_x_positions[xi], destination_y_positions[yi])) return;
            }
        }
        g_dma2d_test_case = 29u;
        if (!run_m2m_strided_at(56u, 56u, 0u, 0u)) return;
    }
    g_dma2d_test_stage = DMA2D_SELFTEST_STAGE_COMPLETE;
    ++g_dma2d_test_pass;
    g_dma2d_test_state = DMA2D_SELFTEST_STATE_PASS;
}

#endif /* XHGC_DMA2D_SELFTEST_ENABLE */
