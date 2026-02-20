#include "cart_reader.h"
#include "ff.h"
#include <string.h>

static FATFS s_fs;
static int s_fs_ready = 0;

int cart_fs_init(void)
{
  FRESULT fr = f_mount(&s_fs, "0:", 1);
  s_fs_ready = (fr == FR_OK);
  return s_fs_ready ? 0 : -1;
}

int cart_read_title_from_sd(const char* path, char out_title[65])
{
  if (!out_title) return -10;
  out_title[0] = '\0';

  if (!s_fs_ready) {
    if (cart_fs_init() != 0) return -11;
  }

  FIL fp;
  UINT br = 0;

  FRESULT fr = f_open(&fp, path, FA_READ);
  if (fr != FR_OK) return -1;

  fr = f_lseek(&fp, 0x001C);
  if (fr != FR_OK) { f_close(&fp); return -2; }

  memset(out_title, 0, 65);
  fr = f_read(&fp, out_title, 64, &br);
  f_close(&fp);

  if (fr != FR_OK || br != 64) return -3;
  out_title[64] = '\0';
  return 0;
}

int cart_read_preview_from_sd(const char* path, uint8_t* out_buf, uint32_t buf_size)
{
  if (!out_buf) return -10;
  if (buf_size < IMG_SIZE) return -11;

  if (!s_fs_ready) {
    if (cart_fs_init() != 0) return -12;
  }

  FIL  fp;
  UINT br = 0;
  FRESULT fr;

  fr = f_open(&fp, path, FA_READ);
  if (fr != FR_OK) return -1;

  fr = f_lseek(&fp, 0x1000u);
  if (fr != FR_OK) { f_close(&fp); return -2; }

  const uint32_t ROW_BYTES = IMG_STRIDE;  /* 800 bytes */
  static uint8_t row_buf[IMG_STRIDE];     /* 静态小缓冲，避免DMA直写SDRAM问题 */

  for (int row = 0; row < IMG_H; row++) {
    fr = f_read(&fp, row_buf, ROW_BYTES, &br);
    if (fr != FR_OK || br != ROW_BYTES) {
      f_close(&fp);
      return -7;
    }
    memcpy(out_buf + row * ROW_BYTES, row_buf, ROW_BYTES);
  }

  f_close(&fp);
  return 0;
}