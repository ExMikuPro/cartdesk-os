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

int cart_bin_fs_init(void);
int cart_bin_read_title_from_sd(const char *path, char out_title[CART_BIN_TITLE_BUFFER_SIZE]);
int cart_bin_read_preview_from_sd(const char *path, uint8_t *buffer, uint32_t size);

#endif
