#include "xhgc_cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMAGE_SIZE 0x5000u
#define TEST_MANF_OFF   0x2000u
#define TEST_INDEX_OFF  0x3000u
#define TEST_DATA_OFF   0x4000u

typedef struct {
    const uint8_t *data;
    uint64_t size;
} MemReader;

static int g_failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

#define EXPECT_EQ_INT(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_STREQ(a, b) EXPECT_TRUE(strcmp((a), (b)) == 0)

static uint32_t crc32_ieee(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static int mem_reader(void *ctx, uint64_t offset, void *buf, uint32_t size)
{
    MemReader *r = (MemReader *)ctx;
    if (!r || !buf) return -1;
    if (offset + size > r->size) return -1;
    memcpy(buf, r->data + offset, size);
    return 0;
}

static void put_fixed(uint8_t *dst, uint32_t size, const char *s)
{
    size_t n = strlen(s);
    if (n > size) n = size;
    memcpy(dst, s, n);
}

static void put_slot(uint8_t *image, uint32_t slot, uint64_t offset, uint32_t size, uint32_t crc)
{
    uint8_t *p = image + XHGC_CART_ADDR_TABLE_OFFSET + slot * XHGC_CART_ADDR_SLOT_SIZE;
    wr64(p, offset);
    wr32(p + 8, size);
    wr32(p + 12, crc);
}

static uint32_t put_manf_string(uint8_t *manf,
                                uint8_t *table,
                                uint32_t index,
                                uint8_t field_id,
                                uint32_t cursor,
                                const char *value)
{
    uint32_t len = (uint32_t)strlen(value);
    table[index * 8u] = field_id;
    wr32(table + index * 8u + 4u, cursor);
    wr16(manf + cursor, (uint16_t)len);
    memcpy(manf + cursor + 2u, value, len);
    return cursor + 2u + len;
}

static uint32_t put_manf_u64(uint8_t *manf,
                             uint8_t *table,
                             uint32_t index,
                             uint8_t field_id,
                             uint32_t cursor,
                             uint64_t value)
{
    table[index * 8u] = field_id;
    wr32(table + index * 8u + 4u, cursor);
    wr16(manf + cursor, 8u);
    wr64(manf + cursor + 2u, value);
    return cursor + 10u;
}

static void build_valid_image(uint8_t *image)
{
    const char main_lua[] = "print('hello xhgc')\n";
    const char readme[] = "readme";
    uint32_t cursor;
    uint8_t *manf = image + TEST_MANF_OFF;
    uint8_t *index = image + TEST_INDEX_OFF;
    uint8_t *data = image + TEST_DATA_OFF;
    uint8_t *table;
    uint32_t manf_size;
    uint32_t index_size;
    uint32_t header_crc;

    memset(image, 0, TEST_IMAGE_SIZE);

    memcpy(image, XHGC_CART_MAGIC, XHGC_CART_MAGIC_SIZE);
    wr32(image + 0x0008u, XHGC_CART_HEADER_VERSION);
    wr32(image + 0x000Cu, XHGC_CART_HEADER_SIZE);
    wr64(image + 0x0014u, 0x0123456789ABCDEFULL);
    put_fixed(image + 0x001Cu, XHGC_CART_TITLE_SIZE, "Hatsune Miku");
    put_fixed(image + 0x005Cu, XHGC_CART_TITLE_ZH_SIZE, "Demo Zh");
    put_fixed(image + 0x009Cu, XHGC_CART_PUBLISHER_SIZE, "Nixie Studio");
    put_fixed(image + 0x00DCu, XHGC_CART_VERSION_SIZE, "0.1.0");
    put_fixed(image + 0x00FCu, XHGC_CART_ENTRY_SIZE, "app/main.lua");
    put_fixed(image + 0x017Cu, XHGC_CART_MIN_FW_SIZE, "0.8.0");

    wr32(manf, 0x464E414Du);
    wr32(manf + 4u, 1u);
    wr32(manf + 12u, 7u);
    table = manf + 16u;
    cursor = 16u + 7u * 8u;
    cursor = put_manf_string(manf, table, 0u, 0x01u, cursor, "Hatsune Miku");
    cursor = put_manf_string(manf, table, 1u, 0x02u, cursor, "Demo Zh");
    cursor = put_manf_string(manf, table, 2u, 0x03u, cursor, "Nixie Studio");
    cursor = put_manf_string(manf, table, 3u, 0x04u, cursor, "0.1.0");
    cursor = put_manf_u64(manf, table, 4u, 0x05u, cursor, 0x0123456789ABCDEFULL);
    cursor = put_manf_string(manf, table, 5u, 0x06u, cursor, "app/main.lua");
    cursor = put_manf_string(manf, table, 6u, 0x07u, cursor, "0.8.0");
    manf_size = cursor;
    wr32(manf + 8u, manf_size);

    memcpy(data, main_lua, sizeof(main_lua) - 1u);
    memcpy(data + sizeof(main_lua) - 1u, readme, sizeof(readme) - 1u);

    wr32(index, 2u);
    cursor = 8u;
    wr32(index + cursor, 0u);
    wr32(index + cursor + 4u, (uint32_t)(sizeof(main_lua) - 1u));
    wr32(index + cursor + 8u, crc32_ieee((const uint8_t *)main_lua, sizeof(main_lua) - 1u));
    index[cursor + 12u] = 12u;
    cursor += 16u;
    memcpy(index + cursor, "app/main.lua", 12u);
    cursor += 12u;
    wr32(index + cursor, (uint32_t)(sizeof(main_lua) - 1u));
    wr32(index + cursor + 4u, (uint32_t)(sizeof(readme) - 1u));
    wr32(index + cursor + 8u, crc32_ieee((const uint8_t *)readme, sizeof(readme) - 1u));
    index[cursor + 12u] = 10u;
    cursor += 16u;
    memcpy(index + cursor, "doc/readme", 10u);
    cursor += 10u;
    index_size = cursor;

    put_slot(image, XHGC_CART_SLOT_MANF, TEST_MANF_OFF, manf_size, crc32_ieee(manf, manf_size));
    put_slot(image, XHGC_CART_SLOT_INDEX, TEST_INDEX_OFF, index_size, crc32_ieee(index, index_size));
    put_slot(image, XHGC_CART_SLOT_DATA, TEST_DATA_OFF,
             (uint32_t)(sizeof(main_lua) + sizeof(readme) - 2u),
             crc32_ieee(data, (uint32_t)(sizeof(main_lua) + sizeof(readme) - 2u)));

    memset(image + XHGC_CART_HEADER_CRC_OFFSET, 0, 4u);
    header_crc = crc32_ieee(image, XHGC_CART_HEADER_SIZE);
    wr32(image + XHGC_CART_HEADER_CRC_OFFSET, header_crc);
}

static XHGC_Cart open_image(uint8_t *image, uint64_t size, MemReader *reader, int *out_rc)
{
    XHGC_Cart cart;
    reader->data = image;
    reader->size = size;
    *out_rc = xhgc_cart_open_reader(&cart, mem_reader, reader, size);
    return cart;
}

static void test_valid_header_and_manf(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    MemReader reader;
    int rc;
    XHGC_CartManf manf;
    XHGC_Cart cart;

    build_valid_image(image);
    cart = open_image(image, sizeof(image), &reader, &rc);

    EXPECT_EQ_INT(rc, XHGC_CART_OK);
    EXPECT_STREQ(cart.header.title, "Hatsune Miku");
    EXPECT_STREQ(cart.header.entry, "app/main.lua");
    EXPECT_TRUE(cart.header.cart_id == 0x0123456789ABCDEFULL);

    rc = xhgc_cart_read_manf(&cart, &manf);
    EXPECT_EQ_INT(rc, XHGC_CART_OK);
    EXPECT_STREQ(manf.title, "Hatsune Miku");
    EXPECT_STREQ(manf.publisher, "Nixie Studio");
    EXPECT_STREQ(manf.version, "0.1.0");
    EXPECT_STREQ(manf.entry, "app/main.lua");
    EXPECT_TRUE(manf.cart_id == 0x0123456789ABCDEFULL);
}

static void test_bad_magic_and_crc(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    MemReader reader;
    int rc;

    build_valid_image(image);
    image[0] = 'Y';
    (void)open_image(image, sizeof(image), &reader, &rc);
    EXPECT_EQ_INT(rc, XHGC_CART_E_MAGIC);

    build_valid_image(image);
    image[0x20u] ^= 0x55u;
    (void)open_image(image, sizeof(image), &reader, &rc);
    EXPECT_EQ_INT(rc, XHGC_CART_E_CRC);
}

static void test_index_find_and_read(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    MemReader reader;
    int rc;
    XHGC_Cart cart;
    XHGC_CartFile file;
    char buf[64];
    uint32_t read_size = 0;

    build_valid_image(image);
    cart = open_image(image, sizeof(image), &reader, &rc);
    EXPECT_EQ_INT(rc, XHGC_CART_OK);

    rc = xhgc_cart_find_file(&cart, "app/main.lua", &file);
    EXPECT_EQ_INT(rc, XHGC_CART_OK);
    EXPECT_TRUE(file.image_offset == TEST_DATA_OFF);

    memset(buf, 0, sizeof(buf));
    rc = xhgc_cart_read_file_by_path(&cart, "app/main.lua", buf, sizeof(buf), &read_size);
    EXPECT_EQ_INT(rc, XHGC_CART_OK);
    EXPECT_EQ_INT(read_size, 20);
    EXPECT_STREQ(buf, "print('hello xhgc')\n");

    rc = xhgc_cart_find_file(&cart, "missing.lua", &file);
    EXPECT_EQ_INT(rc, XHGC_CART_E_NOT_FOUND);
}

static void test_range_protection(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    MemReader reader;
    int rc;

    build_valid_image(image);
    put_slot(image, XHGC_CART_SLOT_DATA, TEST_IMAGE_SIZE - 2u, 8u, 0u);
    memset(image + XHGC_CART_HEADER_CRC_OFFSET, 0, 4u);
    wr32(image + XHGC_CART_HEADER_CRC_OFFSET, crc32_ieee(image, XHGC_CART_HEADER_SIZE));

    (void)open_image(image, sizeof(image), &reader, &rc);
    EXPECT_EQ_INT(rc, XHGC_CART_E_RANGE);
}

int main(void)
{
    test_valid_header_and_manf();
    test_bad_magic_and_crc();
    test_index_find_and_read();
    test_range_protection();

    if (g_failures != 0) {
        printf("%d test failure(s)\n", g_failures);
        return 1;
    }

    printf("xhgc_cart_host_test: ok\n");
    return 0;
}
