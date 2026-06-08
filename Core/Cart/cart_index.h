#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t path_hash;
  uint32_t data_off;
  uint32_t size;
  uint32_t crc32;
  uint8_t type;
  uint8_t format;
  uint16_t width;
  uint16_t height;
  const char *path;
} cart_res_meta_t;

bool cart_index_load(const char *cart_path);
void cart_index_reset(void);
bool cart_index_is_loaded(void);
const cart_res_meta_t *cart_index_find(const char *path);
const cart_res_meta_t *cart_index_get(uint16_t index);
uint16_t cart_index_count(void);
const char *cart_index_last_error(void);

bool cart_path_is_valid(const char *path);
bool cart_read_data(uint32_t data_off, void *dst, uint32_t size);

#ifdef __cplusplus
}
#endif
