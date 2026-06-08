#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cart_index.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t index;
  uint16_t generation;
} res_handle_t;

typedef enum {
  RES_INDEXED = 0,
  RES_LOADING,
  RES_READY,
  RES_READY_UNUSED,
  RES_FAILED,
} res_state_t;

typedef enum {
  RES_LIFE_SCENE = 0,
  RES_LIFE_APP,
} res_lifetime_t;

typedef struct {
  void *pixels;
  uint32_t size;
  uint16_t width;
  uint16_t height;
  uint8_t format;
  uint32_t crc32;
} image_resource_t;

typedef struct {
  const cart_res_meta_t *meta;
  image_resource_t image;
  uint16_t refcount;
  uint16_t generation;
  res_lifetime_t lifetime;
  res_state_t state;
} res_record_t;

void res_manager_init(void);
bool res_manager_mount_cart(const char *cart_path);
res_handle_t res_acquire_image(const char *path, res_lifetime_t life);
void res_release(res_handle_t h);
const image_resource_t *res_get_image(res_handle_t h);
void res_scene_reset(void);
bool res_handle_valid(res_handle_t h);
const char *res_last_error(void);

#ifdef __cplusplus
}
#endif
