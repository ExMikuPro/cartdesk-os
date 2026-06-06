#ifndef CART_READER_H
#define CART_READER_H
#include <stdint.h>
#include "sdram_layout.h"

#define SDRAM_IMG_BASE    SDRAM_LAUNCHER_CACHE_ADDR
#define IMG_W             200
#define IMG_H             200
#define IMG_BPP           4                          /* ARGB8888 */
#define IMG_STRIDE        (IMG_W * IMG_BPP)
#define IMG_SIZE          (IMG_W * IMG_H * IMG_BPP)  /* 0x3E800 */
#define IMG_SLOT_STRIDE   IMG_SIZE                   /* 每槽间距，可对齐到 0x40000 */

/* 第 i 个图片槽在 SDRAM 中的起始地址 */
#define SDRAM_IMG_SLOT(i) (SDRAM_IMG_BASE + (uint32_t)(i) * IMG_SLOT_STRIDE)

int cart_fs_init(void);
int cart_read_title_from_sd(const char* path, char out_title[65]);
int cart_read_preview_from_sd(const char* path, uint8_t* buffer, uint32_t size);

#endif
