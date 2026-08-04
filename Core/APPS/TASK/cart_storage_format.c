#include "cart_storage_format.h"

#include <stddef.h>

#include "crc.h"

static uint16_t load_u16_le(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

static uint32_t load_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static void store_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void store_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

void CartStorageFormat_EncodeHeader(
    uint8_t header[CART_STORAGE_HEADER_SIZE],
    uint16_t entry_count,
    const void *payload,
    uint32_t payload_size)
{
    if (header == NULL) {
        return;
    }
    store_u32_le(header, CART_STORAGE_MAGIC);
    store_u16_le(header + 4u, CART_STORAGE_VERSION);
    store_u16_le(header + 6u, entry_count);
    store_u32_le(header + 8u, payload_size);
    store_u32_le(header + 12u,
                 CRC32_IEEE_Calculate(payload, payload_size));
}

bool CartStorageFormat_DecodeHeader(
    const uint8_t header[CART_STORAGE_HEADER_SIZE],
    uint32_t payload_capacity,
    cart_storage_metadata_t *metadata)
{
    if (header == NULL || metadata == NULL ||
        load_u32_le(header) != CART_STORAGE_MAGIC ||
        load_u16_le(header + 4u) != CART_STORAGE_VERSION) {
        return false;
    }
    metadata->entry_count = load_u16_le(header + 6u);
    metadata->payload_size = load_u32_le(header + 8u);
    metadata->crc32 = load_u32_le(header + 12u);
    return metadata->payload_size <= payload_capacity;
}

bool CartStorageFormat_VerifyPayload(const cart_storage_metadata_t *metadata,
                                     const void *payload,
                                     uint32_t payload_size)
{
    if (metadata == NULL || metadata->payload_size != payload_size ||
        (payload == NULL && payload_size != 0u)) {
        return false;
    }
    return CRC32_IEEE_Calculate(payload, payload_size) == metadata->crc32;
}
