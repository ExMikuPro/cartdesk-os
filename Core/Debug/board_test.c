#include "board_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "i2c.h"
#include "lcd.h"
#include "lvgl.h"
#include "tim.h"

#define GT_ADDR7 0x5D
#define GT_ADDR  (GT_ADDR7 << 1)

#define FLASH_TEST_OFF 0x00000000u
#define FLASH_TEST_LEN 256u

static lv_obj_t *s_anim_box = NULL;
static lv_obj_t *s_drag_box = NULL;
static lv_obj_t *s_drag_label = NULL;

static bool s_dragging = false;
static int16_t s_drag_off_x = 0;
static int16_t s_drag_off_y = 0;

static uint8_t s_flash_tx[FLASH_TEST_LEN];
static uint8_t s_flash_rx[FLASH_TEST_LEN];

static void board_test_breakpoint(void)
{
#if defined(__thumb__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || \
    defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
  __BKPT(0);
#endif
}

static void box_anim_x(void *obj, int32_t v)
{
  lv_obj_set_x((lv_obj_t *)obj, v);
}

void BoardTest_StartMovingBox(void)
{
  lv_obj_clean(lv_screen_active());

  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);

  const int32_t box_w = 40;
  const int32_t box_h = 40;

  s_anim_box = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_anim_box, box_w, box_h);
  lv_obj_set_style_bg_opa(s_anim_box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_anim_box, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_border_width(s_anim_box, 0, 0);
  lv_obj_set_style_radius(s_anim_box, 0, 0);
  lv_obj_set_y(s_anim_box, (LCD_H - box_h) / 2);
  lv_obj_set_x(s_anim_box, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_anim_box);
  lv_anim_set_exec_cb(&a, box_anim_x);
  lv_anim_set_values(&a, 0, LCD_W - box_w);
  lv_anim_set_time(&a, 1000);
  lv_anim_set_playback_time(&a, 1000);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

void ui_test_moving_box_start(void)
{
  BoardTest_StartMovingBox();
}

void BoardTest_ReadGt814d(void)
{
  uint8_t n = 0;
  HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, GT_ADDR,
                                          0x814D, I2C_MEMADD_SIZE_16BIT,
                                          &n, 1, 100);

  printf("[GT911] reg 0x814D st=%d val=0x%02X\r\n", (int)st, n);
  board_test_breakpoint();
}

static void drag_box_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  if (!indev) return;

  lv_point_t p;
  lv_indev_get_point(indev, &p);

  if (code == LV_EVENT_PRESSED) {
    s_dragging = true;

    int16_t obj_x = lv_obj_get_x(obj);
    int16_t obj_y = lv_obj_get_y(obj);
    s_drag_off_x = p.x - obj_x;
    s_drag_off_y = p.y - obj_y;
  } else if (code == LV_EVENT_PRESSING) {
    if (!s_dragging) return;

    int16_t new_x = p.x - s_drag_off_x;
    int16_t new_y = p.y - s_drag_off_y;

    lv_obj_t *parent = lv_obj_get_parent(obj);
    int16_t pw = (int16_t)lv_obj_get_width(parent);
    int16_t ph = (int16_t)lv_obj_get_height(parent);
    int16_t ow = (int16_t)lv_obj_get_width(obj);
    int16_t oh = (int16_t)lv_obj_get_height(obj);

    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > pw - ow) new_x = pw - ow;
    if (new_y > ph - oh) new_y = ph - oh;

    lv_obj_set_pos(obj, new_x, new_y);

    if (s_drag_label) {
      char buf[64];
      lv_snprintf(buf, sizeof(buf), "x=%d y=%d (touch %d,%d)",
                  (int)new_x, (int)new_y, (int)p.x, (int)p.y);
      lv_label_set_text(s_drag_label, buf);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    s_dragging = false;
  }
}

void BoardTest_StartTouchDrag(void)
{
  lv_obj_t *scr = lv_screen_active();

  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);

  s_drag_label = lv_label_create(scr);
  lv_label_set_text(s_drag_label, "Hold the square and drag");
  lv_obj_align(s_drag_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_color(s_drag_label, lv_color_hex(0xFFCC00), LV_PART_MAIN);

  s_drag_box = lv_obj_create(scr);
  lv_obj_set_size(s_drag_box, 120, 120);
  lv_obj_set_pos(s_drag_box, 50, 80);
  lv_obj_set_style_radius(s_drag_box, 12, 0);
  lv_obj_set_style_bg_color(s_drag_box, lv_color_hex(0x3DCCA3), 0);
  lv_obj_set_style_border_width(s_drag_box, 0, 0);

  lv_obj_add_flag(s_drag_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_drag_box, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(s_drag_box, drag_box_event_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(s_drag_box, drag_box_event_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(s_drag_box, drag_box_event_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(s_drag_box, drag_box_event_cb, LV_EVENT_PRESS_LOST, NULL);

  lv_obj_t *text = lv_label_create(s_drag_box);
  lv_label_set_text(text, "awa");
  lv_obj_center(text);
}

void ui_test_touch_drag_start(void)
{
  BoardTest_StartTouchDrag();
}

static void print_flash_error(FLASH_Handle *flash)
{
  const FLASH_ErrorInfo *e = FLASH_LastError(flash);
  if (e) {
    printf("[FLASH] HAL fail code=%u step=%s line=%lu addr=0x%08lX\r\n",
           (unsigned)e->code, e->step ? e->step : "?",
           (unsigned long)e->line, (unsigned long)e->addr);
  }
}

void BoardTest_FlashWriteSmoke(FLASH_Handle *flash)
{
  if (!flash) return;

  for (uint32_t i = 0; i < FLASH_TEST_LEN; i++) {
    s_flash_tx[i] = (uint8_t)(i ^ 0xA5);
  }

  (void)FLASH_DisableMemoryMapped(flash);

  memset(s_flash_rx, 0, FLASH_TEST_LEN);
  FLASH_Status st = FLASH_Read(flash, FLASH_TEST_OFF, s_flash_rx, FLASH_TEST_LEN);
  if (st != FLASH_OK) goto fail;
  printf("[FLASH] before erase [0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         s_flash_rx[0], s_flash_rx[1], s_flash_rx[2], s_flash_rx[3],
         s_flash_rx[4], s_flash_rx[5], s_flash_rx[6], s_flash_rx[7]);

  st = FLASH_Erase4K(flash, FLASH_TEST_OFF);
  if (st != FLASH_OK) goto fail;

  memset(s_flash_rx, 0, FLASH_TEST_LEN);
  st = FLASH_Read(flash, FLASH_TEST_OFF, s_flash_rx, FLASH_TEST_LEN);
  if (st != FLASH_OK) goto fail;

  bool all_ff = true;
  for (uint32_t i = 0; i < FLASH_TEST_LEN; i++) {
    if (s_flash_rx[i] != 0xFF) {
      all_ff = false;
      break;
    }
  }
  printf("[FLASH] after erase: %s (rx[0]=%02X)\r\n",
         all_ff ? "all 0xFF OK" : "ERASE FAIL!", s_flash_rx[0]);
  if (!all_ff) goto fail;

  st = FLASH_Prog(flash, FLASH_TEST_OFF, s_flash_tx, FLASH_TEST_LEN);
  if (st != FLASH_OK) goto fail;

  memset(s_flash_rx, 0, FLASH_TEST_LEN);
  st = FLASH_Read(flash, FLASH_TEST_OFF, s_flash_rx, FLASH_TEST_LEN);
  if (st != FLASH_OK) goto fail;

  if (memcmp(s_flash_tx, s_flash_rx, FLASH_TEST_LEN) == 0) {
    printf("[FLASH] single-line read: PASS\r\n");
  } else {
    printf("[FLASH] single-line read: FAIL first diff: tx[i]=%02X rx[i]=%02X\r\n",
           s_flash_tx[0], s_flash_rx[0]);
    printf("[FLASH]   tx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           s_flash_tx[0], s_flash_tx[1], s_flash_tx[2], s_flash_tx[3],
           s_flash_tx[4], s_flash_tx[5], s_flash_tx[6], s_flash_tx[7]);
    printf("[FLASH]   rx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           s_flash_rx[0], s_flash_rx[1], s_flash_rx[2], s_flash_rx[3],
           s_flash_rx[4], s_flash_rx[5], s_flash_rx[6], s_flash_rx[7]);
    goto fail;
  }

  memset(s_flash_rx, 0, FLASH_TEST_LEN);
  st = FLASH_ReadFastQuad(flash, FLASH_TEST_OFF, s_flash_rx, FLASH_TEST_LEN);
  if (st != FLASH_OK) goto fail;

  if (memcmp(s_flash_tx, s_flash_rx, FLASH_TEST_LEN) == 0) {
    printf("[FLASH] quad fast read: PASS\r\n");
    printf("[FLASH] write smoke ALL OK\r\n");
  } else {
    printf("[FLASH] quad fast read: FAIL (dummy cycles or AlternateBytes issue)\r\n");
    printf("[FLASH]   tx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           s_flash_tx[0], s_flash_tx[1], s_flash_tx[2], s_flash_tx[3],
           s_flash_tx[4], s_flash_tx[5], s_flash_tx[6], s_flash_tx[7]);
    printf("[FLASH]   rx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           s_flash_rx[0], s_flash_rx[1], s_flash_rx[2], s_flash_rx[3],
           s_flash_rx[4], s_flash_rx[5], s_flash_rx[6], s_flash_rx[7]);
    printf("[FLASH]   check FLASH_ReadFastQuad dummy cycles and alternate bytes\r\n");
    goto fail;
  }

  (void)FLASH_EnableMemoryMapped(flash);
  return;

fail:
  print_flash_error(flash);
  (void)FLASH_EnableMemoryMapped(flash);
}

void BoardTest_RunFlashStartupDiagnostics(FLASH_Handle *flash)
{
  if (!flash) return;

  uint32_t jedec = 0;
  FLASH_Status st = FLASH_ReadJEDEC(flash, &jedec);
  printf("[FLASH] JEDEC = 0x%06lX (expect Winbond W25Q256: 0xEF4019)\r\n",
         (unsigned long)jedec);
  while (st != FLASH_OK) {
  }

  (void)FLASH_EnableMemoryMapped(flash);

  uintptr_t mm_base = flash->mm_base ? (uintptr_t)flash->mm_base : (uintptr_t)FLASH_MM_BASE;
  volatile const uint8_t *mm = (volatile const uint8_t *)mm_base;
  printf("[FLASH] MM[0..15] =");
  for (int i = 0; i < 16; i++) {
    printf(" %02X", mm[i]);
  }
  printf("\r\n");

  BoardTest_FlashWriteSmoke(flash);
}
