#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cart_index.h"
#include "task_messages.h"

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
  const void *bytes;
  uint32_t size;
} data_resource_t;

typedef enum {
  RES_KIND_NONE = 0,
  RES_KIND_IMAGE,
  RES_KIND_DATA,
} res_kind_t;

typedef struct {
  const cart_res_meta_t *meta;
  image_resource_t image;
  data_resource_t data;
  uint32_t request_id;
  const char *error;
  uint16_t refcount;
  uint16_t generation;
  res_lifetime_t lifetime;
  res_state_t state;
  res_kind_t kind;
} res_record_t;

void res_manager_init(void);
bool res_manager_mount_cart(const char *cart_path);
bool res_manager_mount_cart_async(const char *cart_path,
                                  uint32_t owner_id,
                                  uint32_t owner_generation);
bool res_manager_mount_complete(void);
bool res_manager_mount_ready(void);
bool res_manager_handle_io_completion(const cart_io_completion_t *completion);
res_handle_t res_acquire_image(const char *path, res_lifetime_t life);
res_handle_t res_acquire_data(const char *path, res_lifetime_t life);
void *res_alloc_image_view_buffer(size_t size, size_t align);
void res_release(res_handle_t h);
bool res_retain(res_handle_t h);
const image_resource_t *res_get_image(res_handle_t h);
const data_resource_t *res_get_data(res_handle_t h);
bool res_get_image_dimensions(res_handle_t h, uint16_t *width,
                              uint16_t *height);
uint32_t res_get_storage_size(res_handle_t h);
res_state_t res_handle_state(res_handle_t h);
const char *res_handle_error(res_handle_t h);
void res_scene_reset(void);
bool res_handle_valid(res_handle_t h);
const char *res_last_error(void);
uint32_t res_manager_used_bytes(void);
uint32_t res_manager_peak_bytes(void);
uint32_t res_manager_capacity_bytes(void);
uint32_t res_manager_alive_count(void);
uint32_t res_manager_indexed_count(void);
uint32_t res_manager_refcount_anomaly_count(void);

#ifdef __cplusplus
}
#endif
