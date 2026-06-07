#ifndef CART_BIN_H
#define CART_BIN_H

#include <stdint.h>

#define CART_BIN_TITLE_OFFSET        0x001Cu
#define CART_BIN_TITLE_SIZE          64u
#define CART_BIN_TITLE_BUFFER_SIZE   (CART_BIN_TITLE_SIZE + 1u)

#define CART_BIN_PREVIEW_OFFSET      0x1000u
#define CART_BIN_PREVIEW_W           200u
#define CART_BIN_PREVIEW_H           200u
#define CART_BIN_PREVIEW_BPP         4u
#define CART_BIN_PREVIEW_STRIDE      (CART_BIN_PREVIEW_W * CART_BIN_PREVIEW_BPP)
#define CART_BIN_PREVIEW_SIZE        (CART_BIN_PREVIEW_W * CART_BIN_PREVIEW_H * CART_BIN_PREVIEW_BPP)

#define CART_BIN_TITLE_ZH_SIZE       64u
#define CART_BIN_PUBLISHER_SIZE      64u
#define CART_BIN_VERSION_SIZE        32u
#define CART_BIN_ENTRY_SIZE          128u
#define CART_BIN_MIN_FW_SIZE         32u
#define CART_BIN_INFO_SLOT_COUNT     15u

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t crc32;
    uint8_t present;
} CartBinSlotInfo;

typedef struct {
    uint32_t header_version;
    uint32_t header_size;
    uint32_t flags;
    uint64_t file_size;
    uint64_t cart_id;
    char title[CART_BIN_TITLE_SIZE + 1u];
    char title_zh[CART_BIN_TITLE_ZH_SIZE + 1u];
    char publisher[CART_BIN_PUBLISHER_SIZE + 1u];
    char version[CART_BIN_VERSION_SIZE + 1u];
    char entry[CART_BIN_ENTRY_SIZE + 1u];
    char min_fw[CART_BIN_MIN_FW_SIZE + 1u];
    CartBinSlotInfo slots[CART_BIN_INFO_SLOT_COUNT];
} CartBinInfo;

int cart_bin_fs_init(void);
int cart_bin_read_title_from_sd(const char *path, char out_title[CART_BIN_TITLE_BUFFER_SIZE]);
int cart_bin_read_preview_from_sd(const char *path, uint8_t *buffer, uint32_t size);
int cart_bin_read_info_from_sd(const char *path, CartBinInfo *out_info);

#endif
