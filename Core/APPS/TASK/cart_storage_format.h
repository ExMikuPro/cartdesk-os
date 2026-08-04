#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CART_STORAGE_HEADER_SIZE 16u
#define CART_STORAGE_MAGIC 0x56534B43u
#define CART_STORAGE_VERSION 1u

typedef struct {
    uint16_t entry_count;
    uint32_t payload_size;
    uint32_t crc32;
} cart_storage_metadata_t;

void CartStorageFormat_EncodeHeader(
    uint8_t header[CART_STORAGE_HEADER_SIZE],
    uint16_t entry_count,
    const void *payload,
    uint32_t payload_size);

bool CartStorageFormat_DecodeHeader(
    const uint8_t header[CART_STORAGE_HEADER_SIZE],
    uint32_t payload_capacity,
    cart_storage_metadata_t *metadata);

bool CartStorageFormat_VerifyPayload(const cart_storage_metadata_t *metadata,
                                     const void *payload,
                                     uint32_t payload_size);
