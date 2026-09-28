#ifndef XHGC_DMA2D_SELFTEST_H
#define XHGC_DMA2D_SELFTEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t cr;
    uint32_t isr;
    uint32_t fgmar;
    uint32_t fgor;
    uint32_t bgmar;
    uint32_t bgor;
    uint32_t fgpfccr;
    uint32_t bgpfccr;
    uint32_t omar;
    uint32_t oor;
    uint32_t opfccr;
    uint32_t nlr;
} DMA2D_SelftestRegisterSnapshot;

#if XHGC_DMA2D_SELFTEST_ENABLE

enum {
    DMA2D_SELFTEST_COMMAND_NONE = 0u,
    DMA2D_SELFTEST_COMMAND_R2M_FILL = 1u
};

enum {
    DMA2D_SELFTEST_STATE_IDLE = 0u,
    DMA2D_SELFTEST_STATE_RUNNING = 1u,
    DMA2D_SELFTEST_STATE_PASS = 2u,
    DMA2D_SELFTEST_STATE_FAIL = 3u
};

enum {
    DMA2D_SELFTEST_STAGE_IDLE = 0u,
    DMA2D_SELFTEST_STAGE_ALLOCATE = 1u,
    DMA2D_SELFTEST_STAGE_INITIALIZE = 2u,
    DMA2D_SELFTEST_STAGE_DMA2D = 3u,
    DMA2D_SELFTEST_STAGE_GUARD = 4u,
    DMA2D_SELFTEST_STAGE_COMPARE = 5u,
    DMA2D_SELFTEST_STAGE_COMPLETE = 6u
};

/* GDB writes only command; the app task executes the test. */
extern volatile uint32_t g_dma2d_test_command;
extern volatile uint32_t g_dma2d_test_state;
extern volatile uint32_t g_dma2d_test_stage;
extern volatile uint32_t g_dma2d_test_pass;
extern volatile uint32_t g_dma2d_test_fail;
extern volatile uint32_t g_dma2d_test_case;
extern volatile uint32_t g_dma2d_test_error_x;
extern volatile uint32_t g_dma2d_test_error_y;
extern volatile uint32_t g_dma2d_test_expected;
extern volatile uint32_t g_dma2d_test_actual;
extern volatile uint32_t g_dma2d_test_cycles;
extern volatile uint32_t g_dma2d_test_time_us;
extern volatile uint32_t g_dma2d_test_lom;
extern volatile uint32_t g_dma2d_test_width;
extern volatile uint32_t g_dma2d_test_height;
extern volatile uint32_t g_dma2d_test_source_address;
extern volatile uint32_t g_dma2d_test_destination_address;
extern volatile DMA2D_SelftestRegisterSnapshot g_dma2d_test_registers;

void DMA2D_Selftest_Poll(void);

#else

static inline void DMA2D_Selftest_Poll(void)
{
}

#endif /* XHGC_DMA2D_SELFTEST_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* XHGC_DMA2D_SELFTEST_H */
