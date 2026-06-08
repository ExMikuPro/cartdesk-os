#ifndef XHGC_CART_H
#define XHGC_CART_H

#include <stdint.h>
#include <stdbool.h>

#ifndef XHGC_CART_NO_FATFS
#include "ff.h"
#endif

#ifndef XHGC_CART_NO_FLASH
#include "flash.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define XHGC_CART_MAGIC                "XHGC_PAC"
#define XHGC_CART_MAGIC_SIZE           8u
#define XHGC_CART_HEADER_VERSION       2u
#define XHGC_CART_HEADER_SIZE          4096u
#define XHGC_CART_SLOT_COUNT           15u
#define XHGC_CART_ADDR_TABLE_OFFSET    0x0F00u
#define XHGC_CART_ADDR_SLOT_SIZE       16u
#define XHGC_CART_HEADER_CRC_OFFSET    0x0FFCu

#define XHGC_CART_TITLE_SIZE           64u
#define XHGC_CART_TITLE_ZH_SIZE        64u
#define XHGC_CART_PUBLISHER_SIZE       64u
#define XHGC_CART_VERSION_SIZE         32u
#define XHGC_CART_ENTRY_SIZE           128u
#define XHGC_CART_MIN_FW_SIZE          32u

typedef enum {
    XHGC_CART_SLOT_ICON      = 0,
    XHGC_CART_SLOT_THMB      = 1,
    XHGC_CART_SLOT_MANF      = 2,
    XHGC_CART_SLOT_ENTRY     = 3,
    XHGC_CART_SLOT_INDEX     = 4,
    XHGC_CART_SLOT_DATA      = 5,
    XHGC_CART_SLOT_BNR       = 6,
    XHGC_CART_SLOT_COVR      = 7,
    XHGC_CART_SLOT_TITLE_A8  = 8,
    XHGC_CART_SLOT_IMAGE_CRC = 14,
} XHGC_CartSlotId;

typedef enum {
    XHGC_CART_OK          = 0,
    XHGC_CART_E_PARAM     = -1,
    XHGC_CART_E_IO        = -2,
    XHGC_CART_E_RANGE     = -3,
    XHGC_CART_E_MAGIC     = -4,
    XHGC_CART_E_VERSION   = -5,
    XHGC_CART_E_HEADER    = -6,
    XHGC_CART_E_CRC       = -7,
    XHGC_CART_E_NOT_FOUND = -8,
    XHGC_CART_E_FORMAT    = -9,
} XHGC_CartResult;

typedef int (*XHGC_CartReader)(void *ctx, uint64_t offset, void *buf, uint32_t size);

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t crc32;
    uint8_t present;
} XHGC_CartSlot;

typedef struct {
    uint32_t header_version;
    uint32_t header_size;
    uint32_t flags;
    uint64_t cart_id;
    char title[XHGC_CART_TITLE_SIZE + 1u];
    char title_zh[XHGC_CART_TITLE_ZH_SIZE + 1u];
    char publisher[XHGC_CART_PUBLISHER_SIZE + 1u];
    char version[XHGC_CART_VERSION_SIZE + 1u];
    char entry[XHGC_CART_ENTRY_SIZE + 1u];
    char min_fw[XHGC_CART_MIN_FW_SIZE + 1u];
    XHGC_CartSlot slots[XHGC_CART_SLOT_COUNT];
    uint32_t header_crc32;
    uint32_t computed_crc32;
} XHGC_CartHeader;

typedef struct {
    XHGC_CartReader read;
    void *reader_ctx;
    uint64_t image_size;
    XHGC_CartHeader header;
} XHGC_Cart;

typedef struct {
    char title[XHGC_CART_TITLE_SIZE + 1u];
    char title_zh[XHGC_CART_TITLE_ZH_SIZE + 1u];
    char publisher[XHGC_CART_PUBLISHER_SIZE + 1u];
    char version[XHGC_CART_VERSION_SIZE + 1u];
    uint64_t cart_id;
    char entry[XHGC_CART_ENTRY_SIZE + 1u];
    char min_fw[XHGC_CART_MIN_FW_SIZE + 1u];
} XHGC_CartManf;

typedef struct {
    uint64_t image_offset;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t crc32;
} XHGC_CartFile;

#define XHGC_INDEX_MAGIC              "XHGCIDX2"
#define XHGC_INDEX_MAGIC_SIZE         8u
#define XHGC_INDEX_VERSION            1u
#define XHGC_INDEX_ENTRY_SIZE         32u

#define XHGC_RES_IMAGE                1u
#define XHGC_RES_SCRIPT               2u
#define XHGC_RES_FONT                 3u
#define XHGC_RES_SOUND                4u

#define XHGC_IMG_NONE                 0u
#define XHGC_IMG_BGRA8888             1u
#define XHGC_IMG_RGB565               2u
#define XHGC_IMG_A8                   3u
#define XHGC_IMG_LVGL_BIN             4u

typedef struct {
    uint32_t path_hash;
    uint32_t path_off;
    uint32_t data_off;
    uint32_t size;
    uint32_t crc32;
    uint8_t type;
    uint8_t format;
    uint16_t width;
    uint16_t height;
    uint16_t flags;
    uint32_t reserved;
} XhgcIndexEntry;

typedef bool (*XHGC_CartResourceVisitor)(const char *path,
                                         const XhgcIndexEntry *entry,
                                         void *ctx);

int xhgc_cart_open_reader(XHGC_Cart *cart,
                          XHGC_CartReader read,
                          void *reader_ctx,
                          uint64_t image_size);

int xhgc_cart_get_slot(const XHGC_Cart *cart,
                       XHGC_CartSlotId slot_id,
                       XHGC_CartSlot *out_slot);

int xhgc_cart_read_manf(const XHGC_Cart *cart, XHGC_CartManf *out_manf);
int xhgc_cart_manf_get_string(const XHGC_Cart *cart,
                              uint8_t field_id,
                              char *out,
                              uint32_t out_size);
int xhgc_cart_manf_get_u64(const XHGC_Cart *cart,
                           uint8_t field_id,
                           uint64_t *out_value);

int xhgc_cart_find_file(const XHGC_Cart *cart,
                        const char *path,
                        XHGC_CartFile *out_file);
bool cart_mount_index(XHGC_Cart *cart);
bool cart_find_resource(XHGC_Cart *cart,
                        const char *path,
                        uint16_t expected_type,
                        XhgcIndexEntry *out_entry);
int xhgc_cart_for_each_resource(const XHGC_Cart *cart,
                                XHGC_CartResourceVisitor visitor,
                                void *ctx);
bool xhgc_cart_path_is_valid(const char *path);
int xhgc_cart_read_file(const XHGC_Cart *cart,
                        const XHGC_CartFile *file,
                        uint32_t file_offset,
                        void *buf,
                        uint32_t size);
int xhgc_cart_read_file_by_path(const XHGC_Cart *cart,
                                const char *path,
                                void *buf,
                                uint32_t buf_size,
                                uint32_t *out_size);

#ifndef XHGC_CART_NO_FATFS
typedef struct {
    FIL file;
    XHGC_Cart cart;
} XHGC_CartFatFs;

int xhgc_cart_open_fatfs(XHGC_CartFatFs *cart_file, const char *path);
void xhgc_cart_close_fatfs(XHGC_CartFatFs *cart_file);
#endif

#ifndef XHGC_CART_NO_FLASH
typedef struct {
    FLASH_Handle *flash;
    uint32_t base_offset;
    XHGC_Cart cart;
} XHGC_CartFlash;

int xhgc_cart_open_flash(XHGC_CartFlash *cart_flash,
                         FLASH_Handle *flash,
                         uint32_t base_offset,
                         uint64_t image_size);
#endif

#ifdef __cplusplus
}
#endif

#endif
