#include "lv_port_disp.h"
#include "Core/APPS/LVGL/lvgl.h"
#include "Core/APPS/LVGL/src/drivers/display/st_ltdc/lv_st_ltdc.h"
#include "../Driver/LCD/lcd.h"   // 你的 lcd.h 里有 LCD_W/LCD_H/FB_SIZE/LCD_GetFB

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
  /* DIRECT 模式下，LVGL已经把像素写进 framebuffer 了，这里啥也不用搬 */
  (void)area;
  (void)px_map;

  /* 如果你把 SDRAM 设成 cacheable，这里需要 clean 对应区域；
     但你现在 MPU 里大概率把 0xD0000000 区域设成了 Non-Cacheable，
     先不管，排障阶段越简单越好。 */

  lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
  // lv_display_t * disp = lv_display_create(LCD_W, LCD_H);
  // lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

  /* 关键：用“正在显示的 Layer1 framebuffer”
     如果你只开了 Layer0，就改成 LCD_GetFB(0) */
  void * fb = (void *)LCD_GetFB(1);
  void * fb1 = (void *)LCD_GetDrawFB(1);


  lv_display_t * disp = lv_st_ltdc_create_direct(fb, fb1, 1);

  /* DIRECT：LVGL直接画到fb里 */
  lv_st_ltdc_create_direct(fb,fb1,1);
  lv_display_set_default(disp);
  // lv_display_set_buffers(disp, fb, NULL, FB_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
  // lv_display_set_flush_cb(disp, disp_flush);
}
