#include "cart_bin.h"

#include "ff.h"
#include "fatfs.h"

#include <string.h>

#define CART_BIN_MAGIC             "XHGC_PAC"
#define CART_BIN_MAGIC_SIZE        8u
#define CART_BIN_HEADER_VERSION    2u
#define CART_BIN_HEADER_SIZE       4096u
#define CART_BIN_HEADER_SIZE_OFF   0x000Cu
#define CART_BIN_FLAGS_OFFSET      0x0010u
#define CART_BIN_CART_ID_OFFSET    0x0014u
#define CART_BIN_TITLE_ZH_OFFSET   0x005Cu
#define CART_BIN_PUBLISHER_OFFSET  0x009Cu
#define CART_BIN_VERSION_OFFSET    0x00DCu
#define CART_BIN_ENTRY_OFFSET      0x00FCu
#define CART_BIN_MIN_FW_OFFSET     0x017Cu
#define CART_BIN_ADDR_TABLE_OFFSET 0x0F00u
#define CART_BIN_ADDR_SLOT_SIZE    16u

static uint32_t prv_read_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t prv_read_le64(const uint8_t *p)
{
    return ((uint64_t)prv_read_le32(p)) | ((uint64_t)prv_read_le32(p + 4u) << 32);
}

static int prv_read_exact(FIL *fp, uint32_t offset, void *buf, UINT size)
{
    UINT br = 0;

    if (f_lseek(fp, offset) != FR_OK) {
        return -1;
    }

    if (f_read(fp, buf, size, &br) != FR_OK || br != size) {
        return -2;
    }

    return 0;
}

static void prv_copy_fixed_string(char *dst, uint32_t dst_size, const uint8_t *src, uint32_t src_size)
{
    uint32_t i = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    while (i + 1u < dst_size && i < src_size && src[i] != 0u) {
        dst[i] = (char)src[i];
        i++;
    }
    dst[i] = '\0';
}

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

int cart_bin_read_info_from_sd(const char *path, CartBinInfo *out_info)
{
    FIL fp;
    uint8_t buf[128];
    FRESULT fr;

    if (out_info == NULL) return -10;
    memset(out_info, 0, sizeof(*out_info));

    if (cart_bin_fs_ensure() != 0) return -11;

    fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) return -1;
    out_info->file_size = (uint64_t)f_size(&fp);

    if (prv_read_exact(&fp, 0x0000u, buf, CART_BIN_MAGIC_SIZE) != 0) {
        f_close(&fp);
        return -2;
    }
    if (memcmp(buf, CART_BIN_MAGIC, CART_BIN_MAGIC_SIZE) != 0) {
        f_close(&fp);
        return -3;
    }

    if (prv_read_exact(&fp, 0x0008u, buf, 20u) != 0) {
        f_close(&fp);
        return -4;
    }

    out_info->header_version = prv_read_le32(buf);
    out_info->header_size = prv_read_le32(buf + 4u);
    out_info->flags = prv_read_le32(buf + 8u);
    if (out_info->header_version != CART_BIN_HEADER_VERSION ||
        out_info->header_size != CART_BIN_HEADER_SIZE) {
        f_close(&fp);
        return -5;
    }

    out_info->cart_id = prv_read_le64(buf + 12u);

    if (prv_read_exact(&fp, CART_BIN_TITLE_OFFSET, buf, CART_BIN_TITLE_SIZE) != 0) {
        f_close(&fp);
        return -6;
    }
    prv_copy_fixed_string(out_info->title, sizeof(out_info->title), buf, CART_BIN_TITLE_SIZE);

    if (prv_read_exact(&fp, CART_BIN_TITLE_ZH_OFFSET, buf, CART_BIN_TITLE_ZH_SIZE) != 0) {
        f_close(&fp);
        return -7;
    }
    prv_copy_fixed_string(out_info->title_zh, sizeof(out_info->title_zh), buf, CART_BIN_TITLE_ZH_SIZE);

    if (prv_read_exact(&fp, CART_BIN_PUBLISHER_OFFSET, buf, CART_BIN_PUBLISHER_SIZE) != 0) {
        f_close(&fp);
        return -8;
    }
    prv_copy_fixed_string(out_info->publisher, sizeof(out_info->publisher), buf, CART_BIN_PUBLISHER_SIZE);

    if (prv_read_exact(&fp, CART_BIN_VERSION_OFFSET, buf, CART_BIN_VERSION_SIZE) != 0) {
        f_close(&fp);
        return -9;
    }
    prv_copy_fixed_string(out_info->version, sizeof(out_info->version), buf, CART_BIN_VERSION_SIZE);

    if (prv_read_exact(&fp, CART_BIN_ENTRY_OFFSET, buf, CART_BIN_ENTRY_SIZE) != 0) {
        f_close(&fp);
        return -12;
    }
    prv_copy_fixed_string(out_info->entry, sizeof(out_info->entry), buf, CART_BIN_ENTRY_SIZE);

    if (prv_read_exact(&fp, CART_BIN_MIN_FW_OFFSET, buf, CART_BIN_MIN_FW_SIZE) != 0) {
        f_close(&fp);
        return -13;
    }
    prv_copy_fixed_string(out_info->min_fw, sizeof(out_info->min_fw), buf, CART_BIN_MIN_FW_SIZE);

    for (uint32_t i = 0; i < CART_BIN_INFO_SLOT_COUNT; ++i) {
        uint32_t offset = CART_BIN_ADDR_TABLE_OFFSET + i * CART_BIN_ADDR_SLOT_SIZE;

        if (prv_read_exact(&fp, offset, buf, CART_BIN_ADDR_SLOT_SIZE) != 0) {
            f_close(&fp);
            return -14;
        }

        out_info->slots[i].offset = prv_read_le64(buf);
        out_info->slots[i].size = prv_read_le32(buf + 8u);
        out_info->slots[i].crc32 = prv_read_le32(buf + 12u);
        out_info->slots[i].present = out_info->slots[i].size != 0u;
    }

    f_close(&fp);
    return 0;
}
