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
