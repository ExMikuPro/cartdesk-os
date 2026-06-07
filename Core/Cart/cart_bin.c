#include "cart_bin.h"

#include "ff.h"
#include "fatfs.h"

#include <string.h>

int cart_bin_fs_init(void)
{
    FRESULT fr = SD_FATFS_Mount();
    return (fr == FR_OK) ? 0 : -1;
}

static int cart_bin_fs_ensure(void)
{
    return cart_bin_fs_init();
}

int cart_bin_read_title_from_sd(const char *path, char out_title[CART_BIN_TITLE_BUFFER_SIZE])
{
    if (!out_title) return -10;
    out_title[0] = '\0';

    if (cart_bin_fs_ensure() != 0) return -11;

    FIL fp;
    UINT br = 0;

    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) return -1;

    fr = f_lseek(&fp, CART_BIN_TITLE_OFFSET);
    if (fr != FR_OK) {
        f_close(&fp);
        return -2;
    }

    memset(out_title, 0, CART_BIN_TITLE_BUFFER_SIZE);
    fr = f_read(&fp, out_title, CART_BIN_TITLE_SIZE, &br);
    f_close(&fp);

    if (fr != FR_OK || br != CART_BIN_TITLE_SIZE) return -3;
    out_title[CART_BIN_TITLE_SIZE] = '\0';
    return 0;
}

int cart_bin_read_preview_from_sd(const char *path, uint8_t *out_buf, uint32_t buf_size)
{
    if (!out_buf) return -10;
    if (buf_size < CART_BIN_PREVIEW_SIZE) return -11;

    if (cart_bin_fs_ensure() != 0) return -12;

    FIL fp;
    UINT br = 0;
    FRESULT fr;

    fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) return -1;

    fr = f_lseek(&fp, CART_BIN_PREVIEW_OFFSET);
    if (fr != FR_OK) {
        f_close(&fp);
        return -2;
    }

    const UINT row_bytes = (UINT)CART_BIN_PREVIEW_STRIDE;
    static uint8_t row_buf[CART_BIN_PREVIEW_STRIDE];

    for (uint32_t row = 0; row < CART_BIN_PREVIEW_H; row++) {
        fr = f_read(&fp, row_buf, row_bytes, &br);
        if (fr != FR_OK || br != row_bytes) {
            f_close(&fp);
            return -7;
        }
        memcpy(out_buf + row * CART_BIN_PREVIEW_STRIDE, row_buf, row_bytes);
    }

    f_close(&fp);
    return 0;
}
