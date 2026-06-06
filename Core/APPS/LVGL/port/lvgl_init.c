/*********************
*      INCLUDES
 *********************/
#include "lvgl_init.h"
#include "lvgl.h"
#include "src/draw/lv_draw_buf_private.h"
#include "lv_port_disp.h"
#include "lv_port_tick.h"
#include "lv_port_indev.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void cache_align_range(const void * addr, uint32_t size, uintptr_t * aligned_start, uint32_t * aligned_size);
static void draw_buf_clean_cache_cb(const lv_draw_buf_t * draw_buf, const lv_area_t * area);
static void draw_buf_invalidate_cache_cb(const lv_draw_buf_t * draw_buf, const lv_area_t * area);
static void setup_draw_buf_cache_handlers(void);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * @brief 初始化LVGL及所有移植接口
 * @note 在main函数中调用，在初始化外设之后
 */
void lvgl_init(void)
{
  /* 1. 初始化LVGL核心 */
  lv_init();

  /* 1.1 补齐 draw_buf 的 D-Cache 回调，确保 DIRECT 双缓冲下 CPU 同步区复制后能正确回写到 SDRAM。 */
  setup_draw_buf_cache_handlers();

  /* 2. 初始化时基 */
  lv_port_tick_init();

  /* 3. 初始化显示驱动 */
  lv_port_disp_init();

  /* 4. 初始化输入设备（触摸屏暂不启用） */
  lv_port_indev_init();
}

/**
 * @brief LVGL任务处理函数
 * @note 需要在主循环中周期性调用，建议5-10ms调用一次
 *
 * 使用示例：
 * while(1) {
 *     lvgl_task_handler();
 *     HAL_Delay(5);
 * }
 */
void lvgl_task_handler(void)
{
  lv_timer_handler();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void cache_align_range(const void * addr, uint32_t size, uintptr_t * aligned_start, uint32_t * aligned_size)
{
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;

  *aligned_start = start & ~(uintptr_t)31U;
  *aligned_size = (uint32_t)(((end + 31U) & ~(uintptr_t)31U) - *aligned_start);
}

static void draw_buf_clean_cache_cb(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
#if defined(__CORTEX_M) && (__CORTEX_M == 7)
  if(!(SCB->CCR & SCB_CCR_DC_Msk)) {
    return;
  }

  uint32_t bpp = lv_color_format_get_bpp(draw_buf->header.cf);
  uint32_t line_bytes = (uint32_t)((lv_area_get_width(area) * (int32_t)bpp + 7) >> 3);
  const uint8_t * row = lv_draw_buf_goto_xy(draw_buf, (uint32_t)area->x1, (uint32_t)area->y1);
  for(int32_t y = area->y1; y <= area->y2; y++) {
    uintptr_t aligned_start;
    uint32_t aligned_size;
    cache_align_range(row, line_bytes, &aligned_start, &aligned_size);
    SCB_CleanDCache_by_Addr((uint32_t *)aligned_start, (int32_t)aligned_size);
    row += draw_buf->header.stride;
  }
#else
  LV_UNUSED(draw_buf);
  LV_UNUSED(area);
#endif
}

static void draw_buf_invalidate_cache_cb(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
#if defined(__CORTEX_M) && (__CORTEX_M == 7)
  if(!(SCB->CCR & SCB_CCR_DC_Msk)) {
    return;
  }

  uint32_t bpp = lv_color_format_get_bpp(draw_buf->header.cf);
  uint32_t line_bytes = (uint32_t)((lv_area_get_width(area) * (int32_t)bpp + 7) >> 3);
  const uint8_t * row = lv_draw_buf_goto_xy(draw_buf, (uint32_t)area->x1, (uint32_t)area->y1);
  for(int32_t y = area->y1; y <= area->y2; y++) {
    uintptr_t aligned_start;
    uint32_t aligned_size;
    cache_align_range(row, line_bytes, &aligned_start, &aligned_size);
    SCB_InvalidateDCache_by_Addr((uint32_t *)aligned_start, (int32_t)aligned_size);
    row += draw_buf->header.stride;
  }
#else
  LV_UNUSED(draw_buf);
  LV_UNUSED(area);
#endif
}

static void setup_draw_buf_cache_handlers(void)
{
  lv_draw_buf_handlers_t * handlers = lv_draw_buf_get_handlers();
  lv_draw_buf_handlers_t * font_handlers = lv_draw_buf_get_font_handlers();
  lv_draw_buf_handlers_t * image_handlers = lv_draw_buf_get_image_handlers();

  handlers->flush_cache_cb = draw_buf_clean_cache_cb;
  handlers->invalidate_cache_cb = draw_buf_invalidate_cache_cb;

  font_handlers->flush_cache_cb = draw_buf_clean_cache_cb;
  font_handlers->invalidate_cache_cb = draw_buf_invalidate_cache_cb;

  image_handlers->flush_cache_cb = draw_buf_clean_cache_cb;
  image_handlers->invalidate_cache_cb = draw_buf_invalidate_cache_cb;
}
