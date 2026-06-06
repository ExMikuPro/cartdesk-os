#include "xhgc_cart.h"

#ifndef XHGC_CART_NO_HW_CRC
#include "crc.h"
#endif

#include <stddef.h>
#include <string.h>

#define XHGC_MANF_MAGIC            0x464E414Du
#define XHGC_MANF_VERSION          1u
#define XHGC_MANF_HEADER_SIZE      16u
#define XHGC_MANF_FIELD_ENTRY_SIZE 8u
#define XHGC_INDEX_HEADER_SIZE     8u
#define XHGC_INDEX_ENTRY_HEAD_SIZE 16u

#define XHGC_MANF_FIELD_TITLE      0x01u
#define XHGC_MANF_FIELD_TITLE_ZH   0x02u
#define XHGC_MANF_FIELD_PUBLISHER  0x03u
#define XHGC_MANF_FIELD_VERSION    0x04u
#define XHGC_MANF_FIELD_CART_ID    0x05u
#define XHGC_MANF_FIELD_ENTRY      0x06u
#define XHGC_MANF_FIELD_MIN_FW     0x07u

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return ((uint64_t)read_le32(p)) | ((uint64_t)read_le32(p + 4u) << 32);
}

static int add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (UINT64_MAX - a < b) return 1;
    *out = a + b;
    return 0;
}

static int range_in_image(uint64_t offset, uint64_t size, uint64_t image_size)
{
    uint64_t end = 0;
    if (add_overflow_u64(offset, size, &end)) return 0;
    return end <= image_size;
}

static void copy_fixed_string(char *dst, uint32_t dst_size, const uint8_t *src, uint32_t src_size)
{
    uint32_t i = 0;
    if (!dst || dst_size == 0u) return;

    while (i + 1u < dst_size && i < src_size && src[i] != 0u) {
        dst[i] = (char)src[i];
        i++;
    }
    dst[i] = '\0';
}

#ifdef XHGC_CART_NO_HW_CRC
static uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t size)
{
    return crc32_ieee_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}
#endif

static uint32_t xhgc_cart_crc32_ieee(const uint8_t *data, uint32_t size)
{
#ifdef XHGC_CART_NO_HW_CRC
    return crc32_ieee(data, size);
#else
    return CRC32_IEEE_Calculate(data, size);
#endif
}

static uint32_t header_crc32_ieee(uint8_t header[XHGC_CART_HEADER_SIZE])
{
    uint8_t saved[4];
    uint32_t crc = 0;

    memcpy(saved, header + XHGC_CART_HEADER_CRC_OFFSET, sizeof(saved));
    memset(header + XHGC_CART_HEADER_CRC_OFFSET, 0, sizeof(saved));
    crc = xhgc_cart_crc32_ieee(header, XHGC_CART_HEADER_SIZE);
    memcpy(header + XHGC_CART_HEADER_CRC_OFFSET, saved, sizeof(saved));

    return crc;
}

static int cart_read_exact(const XHGC_Cart *cart, uint64_t offset, void *buf, uint32_t size)
{
    if (!cart || !cart->read || !buf) return XHGC_CART_E_PARAM;
    if (!range_in_image(offset, size, cart->image_size)) return XHGC_CART_E_RANGE;
    return cart->read(cart->reader_ctx, offset, buf, size) == 0 ? XHGC_CART_OK : XHGC_CART_E_IO;
}

static int get_present_slot(const XHGC_Cart *cart, XHGC_CartSlotId slot_id, XHGC_CartSlot *out_slot)
{
    if (!cart || !out_slot) return XHGC_CART_E_PARAM;
    if ((uint32_t)slot_id >= XHGC_CART_SLOT_COUNT) return XHGC_CART_E_PARAM;
    if (!cart->header.slots[slot_id].present) return XHGC_CART_E_NOT_FOUND;
    *out_slot = cart->header.slots[slot_id];
    return XHGC_CART_OK;
}

int xhgc_cart_open_reader(XHGC_Cart *cart,
                          XHGC_CartReader read,
                          void *reader_ctx,
                          uint64_t image_size)
{
    uint8_t header[XHGC_CART_HEADER_SIZE];

    if (!cart || !read) return XHGC_CART_E_PARAM;
    if (image_size < XHGC_CART_HEADER_SIZE) return XHGC_CART_E_RANGE;

    memset(cart, 0, sizeof(*cart));
    cart->read = read;
    cart->reader_ctx = reader_ctx;
    cart->image_size = image_size;

    if (cart_read_exact(cart, 0u, header, XHGC_CART_HEADER_SIZE) != XHGC_CART_OK) {
        memset(cart, 0, sizeof(*cart));
        return XHGC_CART_E_IO;
    }

    if (memcmp(header, XHGC_CART_MAGIC, XHGC_CART_MAGIC_SIZE) != 0) {
        memset(cart, 0, sizeof(*cart));
        return XHGC_CART_E_MAGIC;
    }

    cart->header.header_version = read_le32(header + 0x0008u);
    cart->header.header_size = read_le32(header + 0x000Cu);
    if (cart->header.header_version != XHGC_CART_HEADER_VERSION) {
        memset(cart, 0, sizeof(*cart));
        return XHGC_CART_E_VERSION;
    }
    if (cart->header.header_size != XHGC_CART_HEADER_SIZE) {
        memset(cart, 0, sizeof(*cart));
        return XHGC_CART_E_HEADER;
    }

    cart->header.header_crc32 = read_le32(header + XHGC_CART_HEADER_CRC_OFFSET);
    cart->header.computed_crc32 = header_crc32_ieee(header);
    if (cart->header.header_crc32 != cart->header.computed_crc32) {
        memset(cart, 0, sizeof(*cart));
        return XHGC_CART_E_CRC;
    }

    cart->header.flags = read_le32(header + 0x0010u);
    cart->header.cart_id = read_le64(header + 0x0014u);
    copy_fixed_string(cart->header.title, sizeof(cart->header.title), header + 0x001Cu, XHGC_CART_TITLE_SIZE);
    copy_fixed_string(cart->header.title_zh, sizeof(cart->header.title_zh), header + 0x005Cu, XHGC_CART_TITLE_ZH_SIZE);
    copy_fixed_string(cart->header.publisher, sizeof(cart->header.publisher), header + 0x009Cu, XHGC_CART_PUBLISHER_SIZE);
    copy_fixed_string(cart->header.version, sizeof(cart->header.version), header + 0x00DCu, XHGC_CART_VERSION_SIZE);
    copy_fixed_string(cart->header.entry, sizeof(cart->header.entry), header + 0x00FCu, XHGC_CART_ENTRY_SIZE);
    copy_fixed_string(cart->header.min_fw, sizeof(cart->header.min_fw), header + 0x017Cu, XHGC_CART_MIN_FW_SIZE);

    for (uint32_t i = 0; i < XHGC_CART_SLOT_COUNT; i++) {
        const uint8_t *slot = header + XHGC_CART_ADDR_TABLE_OFFSET + i * XHGC_CART_ADDR_SLOT_SIZE;
        cart->header.slots[i].offset = read_le64(slot);
        cart->header.slots[i].size = read_le32(slot + 8u);
        cart->header.slots[i].crc32 = read_le32(slot + 12u);
        cart->header.slots[i].present = cart->header.slots[i].size != 0u;

        if (cart->header.slots[i].present &&
            !range_in_image(cart->header.slots[i].offset,
                            cart->header.slots[i].size,
                            cart->image_size)) {
            memset(cart, 0, sizeof(*cart));
            return XHGC_CART_E_RANGE;
        }
    }

    return XHGC_CART_OK;
}

int xhgc_cart_get_slot(const XHGC_Cart *cart,
                       XHGC_CartSlotId slot_id,
                       XHGC_CartSlot *out_slot)
{
    if (!cart || !out_slot) return XHGC_CART_E_PARAM;
    if ((uint32_t)slot_id >= XHGC_CART_SLOT_COUNT) return XHGC_CART_E_PARAM;

    *out_slot = cart->header.slots[slot_id];
    return out_slot->present ? XHGC_CART_OK : XHGC_CART_E_NOT_FOUND;
}

static int read_manf_header(const XHGC_Cart *cart,
                            XHGC_CartSlot *slot,
                            uint32_t *total_size,
                            uint32_t *field_count)
{
    uint8_t buf[XHGC_MANF_HEADER_SIZE];
    uint64_t table_size = 0;
    int rc = get_present_slot(cart, XHGC_CART_SLOT_MANF, slot);
    if (rc != XHGC_CART_OK) return rc;
    if (slot->size < XHGC_MANF_HEADER_SIZE) return XHGC_CART_E_FORMAT;

    rc = cart_read_exact(cart, slot->offset, buf, sizeof(buf));
    if (rc != XHGC_CART_OK) return rc;

    if (read_le32(buf) != XHGC_MANF_MAGIC) return XHGC_CART_E_FORMAT;
    if (read_le32(buf + 4u) != XHGC_MANF_VERSION) return XHGC_CART_E_FORMAT;

    *total_size = read_le32(buf + 8u);
    *field_count = read_le32(buf + 12u);
    if (*total_size != slot->size) return XHGC_CART_E_FORMAT;

    table_size = XHGC_MANF_HEADER_SIZE + (uint64_t)(*field_count) * XHGC_MANF_FIELD_ENTRY_SIZE;
    if (table_size > *total_size) return XHGC_CART_E_RANGE;

    return XHGC_CART_OK;
}

static int manf_find_field(const XHGC_Cart *cart,
                           uint8_t field_id,
                           XHGC_CartSlot *out_slot,
                           uint32_t *out_field_offset,
                           uint16_t *out_field_size)
{
    XHGC_CartSlot slot;
    uint32_t total_size = 0;
    uint32_t field_count = 0;
    int rc = read_manf_header(cart, &slot, &total_size, &field_count);
    if (rc != XHGC_CART_OK) return rc;

    for (uint32_t i = 0; i < field_count; i++) {
        uint8_t ent[XHGC_MANF_FIELD_ENTRY_SIZE];
        uint32_t field_offset = 0;
        uint16_t field_size = 0;
        uint64_t ent_offset = slot.offset + XHGC_MANF_HEADER_SIZE +
                              (uint64_t)i * XHGC_MANF_FIELD_ENTRY_SIZE;

        rc = cart_read_exact(cart, ent_offset, ent, sizeof(ent));
        if (rc != XHGC_CART_OK) return rc;
        if (ent[0] != field_id) continue;

        field_offset = read_le32(ent + 4u);
        if ((uint64_t)field_offset + 2u > total_size) return XHGC_CART_E_RANGE;

        rc = cart_read_exact(cart, slot.offset + field_offset, ent, 2u);
        if (rc != XHGC_CART_OK) return rc;

        field_size = read_le16(ent);
        if ((uint64_t)field_offset + 2u + field_size > total_size) return XHGC_CART_E_RANGE;

        if (out_slot) *out_slot = slot;
        if (out_field_offset) *out_field_offset = field_offset;
        if (out_field_size) *out_field_size = field_size;
        return XHGC_CART_OK;
    }

    return XHGC_CART_E_NOT_FOUND;
}

int xhgc_cart_manf_get_string(const XHGC_Cart *cart,
                              uint8_t field_id,
                              char *out,
                              uint32_t out_size)
{
    XHGC_CartSlot slot;
    uint32_t field_offset = 0;
    uint16_t field_size = 0;
    uint8_t chunk[64];
    uint32_t copied = 0;
    int rc;

    if (!out || out_size == 0u) return XHGC_CART_E_PARAM;
    out[0] = '\0';

    rc = manf_find_field(cart, field_id, &slot, &field_offset, &field_size);
    if (rc != XHGC_CART_OK) return rc;

    while (copied < field_size && copied + 1u < out_size) {
        uint32_t want = (uint32_t)sizeof(chunk);
        uint32_t remain_src = (uint32_t)field_size - copied;
        uint32_t remain_dst = out_size - 1u - copied;
        if (want > remain_src) want = remain_src;
        if (want > remain_dst) want = remain_dst;

        rc = cart_read_exact(cart, slot.offset + field_offset + 2u + copied, chunk, want);
        if (rc != XHGC_CART_OK) return rc;
        memcpy(out + copied, chunk, want);
        copied += want;
    }

    out[copied] = '\0';
    return XHGC_CART_OK;
}

int xhgc_cart_manf_get_u64(const XHGC_Cart *cart,
                           uint8_t field_id,
                           uint64_t *out_value)
{
    XHGC_CartSlot slot;
    uint32_t field_offset = 0;
    uint16_t field_size = 0;
    uint8_t buf[8];
    int rc;

    if (!out_value) return XHGC_CART_E_PARAM;
    *out_value = 0u;

    rc = manf_find_field(cart, field_id, &slot, &field_offset, &field_size);
    if (rc != XHGC_CART_OK) return rc;
    if (field_size != sizeof(buf)) return XHGC_CART_E_FORMAT;

    rc = cart_read_exact(cart, slot.offset + field_offset + 2u, buf, sizeof(buf));
    if (rc != XHGC_CART_OK) return rc;

    *out_value = read_le64(buf);
    return XHGC_CART_OK;
}

int xhgc_cart_read_manf(const XHGC_Cart *cart, XHGC_CartManf *out_manf)
{
    XHGC_CartSlot slot;
    uint32_t total_size = 0;
    uint32_t field_count = 0;
    int rc;

    if (!out_manf) return XHGC_CART_E_PARAM;
    memset(out_manf, 0, sizeof(*out_manf));

    rc = read_manf_header(cart, &slot, &total_size, &field_count);
    if (rc != XHGC_CART_OK) return rc;

    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_TITLE,
                                   out_manf->title, sizeof(out_manf->title));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_TITLE_ZH,
                                   out_manf->title_zh, sizeof(out_manf->title_zh));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_PUBLISHER,
                                   out_manf->publisher, sizeof(out_manf->publisher));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_VERSION,
                                   out_manf->version, sizeof(out_manf->version));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_u64(cart, XHGC_MANF_FIELD_CART_ID, &out_manf->cart_id);
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_ENTRY,
                                   out_manf->entry, sizeof(out_manf->entry));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;
    rc = xhgc_cart_manf_get_string(cart, XHGC_MANF_FIELD_MIN_FW,
                                   out_manf->min_fw, sizeof(out_manf->min_fw));
    if (rc != XHGC_CART_OK && rc != XHGC_CART_E_NOT_FOUND) return rc;

    return XHGC_CART_OK;
}

static uint32_t path_len_u8(const char *path)
{
    uint32_t len = 0;
    if (!path) return 0u;
    while (path[len] != '\0') {
        len++;
        if (len > 255u) return 256u;
    }
    return len;
}

static int index_name_equals(const XHGC_Cart *cart,
                             uint64_t name_offset,
                             const char *path,
                             uint32_t path_len)
{
    uint8_t buf[64];
    uint32_t done = 0;

    while (done < path_len) {
        uint32_t want = (uint32_t)sizeof(buf);
        if (want > path_len - done) want = path_len - done;
        int rc = cart_read_exact(cart, name_offset + done, buf, want);
        if (rc != XHGC_CART_OK) return rc;
        if (memcmp(buf, path + done, want) != 0) return XHGC_CART_E_NOT_FOUND;
        done += want;
    }

    return XHGC_CART_OK;
}

int xhgc_cart_find_file(const XHGC_Cart *cart,
                        const char *path,
                        XHGC_CartFile *out_file)
{
    XHGC_CartSlot index_slot;
    XHGC_CartSlot data_slot;
    uint8_t buf[XHGC_INDEX_ENTRY_HEAD_SIZE];
    uint32_t entry_count = 0;
    uint64_t cursor = XHGC_INDEX_HEADER_SIZE;
    uint32_t path_len = path_len_u8(path);
    int rc;

    if (!cart || !path || !out_file) return XHGC_CART_E_PARAM;
    if (path_len == 0u || path_len > 255u) return XHGC_CART_E_PARAM;

    rc = get_present_slot(cart, XHGC_CART_SLOT_INDEX, &index_slot);
    if (rc != XHGC_CART_OK) return rc;
    rc = get_present_slot(cart, XHGC_CART_SLOT_DATA, &data_slot);
    if (rc != XHGC_CART_OK) return rc;

    if (index_slot.size < XHGC_INDEX_HEADER_SIZE) return XHGC_CART_E_FORMAT;
    rc = cart_read_exact(cart, index_slot.offset, buf, XHGC_INDEX_HEADER_SIZE);
    if (rc != XHGC_CART_OK) return rc;

    entry_count = read_le32(buf);

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t data_offset = 0;
        uint32_t data_size = 0;
        uint32_t file_crc32 = 0;
        uint8_t name_len = 0;
        uint64_t name_offset = 0;
        uint64_t file_image_offset = 0;

        if (cursor + XHGC_INDEX_ENTRY_HEAD_SIZE > index_slot.size) return XHGC_CART_E_RANGE;

        rc = cart_read_exact(cart, index_slot.offset + cursor, buf, XHGC_INDEX_ENTRY_HEAD_SIZE);
        if (rc != XHGC_CART_OK) return rc;

        data_offset = read_le32(buf);
        data_size = read_le32(buf + 4u);
        file_crc32 = read_le32(buf + 8u);
        name_len = buf[12];
        if (name_len == 0u) return XHGC_CART_E_FORMAT;

        cursor += XHGC_INDEX_ENTRY_HEAD_SIZE;
        if (cursor + name_len > index_slot.size) return XHGC_CART_E_RANGE;

        if ((uint64_t)data_offset + data_size > data_slot.size) return XHGC_CART_E_RANGE;
        if (add_overflow_u64(data_slot.offset, data_offset, &file_image_offset)) return XHGC_CART_E_RANGE;
        if (!range_in_image(file_image_offset, data_size, cart->image_size)) return XHGC_CART_E_RANGE;

        name_offset = index_slot.offset + cursor;
        if (name_len == path_len) {
            rc = index_name_equals(cart, name_offset, path, path_len);
            if (rc == XHGC_CART_OK) {
                out_file->image_offset = file_image_offset;
                out_file->data_offset = data_offset;
                out_file->data_size = data_size;
                out_file->crc32 = file_crc32;
                return XHGC_CART_OK;
            }
            if (rc != XHGC_CART_E_NOT_FOUND) return rc;
        }

        cursor += name_len;
    }

    return XHGC_CART_E_NOT_FOUND;
}

int xhgc_cart_read_file(const XHGC_Cart *cart,
                        const XHGC_CartFile *file,
                        uint32_t file_offset,
                        void *buf,
                        uint32_t size)
{
    uint64_t offset = 0;
    if (!cart || !file || !buf) return XHGC_CART_E_PARAM;
    if ((uint64_t)file_offset + size > file->data_size) return XHGC_CART_E_RANGE;
    if (add_overflow_u64(file->image_offset, file_offset, &offset)) return XHGC_CART_E_RANGE;
    return cart_read_exact(cart, offset, buf, size);
}

int xhgc_cart_read_file_by_path(const XHGC_Cart *cart,
                                const char *path,
                                void *buf,
                                uint32_t buf_size,
                                uint32_t *out_size)
{
    XHGC_CartFile file;
    int rc;

    if (!buf) return XHGC_CART_E_PARAM;
    if (out_size) *out_size = 0u;

    rc = xhgc_cart_find_file(cart, path, &file);
    if (rc != XHGC_CART_OK) return rc;
    if (buf_size < file.data_size) return XHGC_CART_E_RANGE;

    rc = xhgc_cart_read_file(cart, &file, 0u, buf, file.data_size);
    if (rc != XHGC_CART_OK) return rc;

    if (out_size) *out_size = file.data_size;
    return XHGC_CART_OK;
}

#ifndef XHGC_CART_NO_FATFS
static int fatfs_reader(void *ctx, uint64_t offset, void *buf, uint32_t size)
{
    FIL *file = (FIL *)ctx;
    UINT br = 0;

    if (!file || !buf) return -1;
    if (offset > UINT32_MAX) return -1;
    if (f_lseek(file, (FSIZE_t)offset) != FR_OK) return -1;
    if (f_read(file, buf, (UINT)size, &br) != FR_OK) return -1;
    return br == size ? 0 : -1;
}

int xhgc_cart_open_fatfs(XHGC_CartFatFs *cart_file, const char *path)
{
    FRESULT fr;
    int rc;

    if (!cart_file || !path) return XHGC_CART_E_PARAM;
    memset(cart_file, 0, sizeof(*cart_file));

    fr = f_open(&cart_file->file, path, FA_READ);
    if (fr != FR_OK) return XHGC_CART_E_IO;

    rc = xhgc_cart_open_reader(&cart_file->cart,
                               fatfs_reader,
                               &cart_file->file,
                               (uint64_t)f_size(&cart_file->file));
    if (rc != XHGC_CART_OK) {
        f_close(&cart_file->file);
        memset(cart_file, 0, sizeof(*cart_file));
        return rc;
    }

    return XHGC_CART_OK;
}

void xhgc_cart_close_fatfs(XHGC_CartFatFs *cart_file)
{
    if (!cart_file) return;
    f_close(&cart_file->file);
    memset(cart_file, 0, sizeof(*cart_file));
}
#endif

#ifndef XHGC_CART_NO_FLASH
static int flash_reader(void *ctx, uint64_t offset, void *buf, uint32_t size)
{
    XHGC_CartFlash *cart_flash = (XHGC_CartFlash *)ctx;
    uint64_t absolute = 0;

    if (!cart_flash || !cart_flash->flash || !buf) return -1;
    if (add_overflow_u64(cart_flash->base_offset, offset, &absolute)) return -1;
    if (absolute > UINT32_MAX) return -1;

    return FLASH_Read(cart_flash->flash, (uint32_t)absolute, buf, size) == FLASH_OK ? 0 : -1;
}

int xhgc_cart_open_flash(XHGC_CartFlash *cart_flash,
                         FLASH_Handle *flash,
                         uint32_t base_offset,
                         uint64_t image_size)
{
    if (!cart_flash || !flash) return XHGC_CART_E_PARAM;
    memset(cart_flash, 0, sizeof(*cart_flash));
    cart_flash->flash = flash;
    cart_flash->base_offset = base_offset;

    return xhgc_cart_open_reader(&cart_flash->cart,
                                 flash_reader,
                                 cart_flash,
                                 image_size);
}
#endif
