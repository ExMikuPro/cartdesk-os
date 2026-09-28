#include "resource_manager.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app_arena.h"
#include "cart_io_service.h"
#include "cart_jpeg_decoder.h"
#include "main.h"
#include "resource_arena_owner.h"
#include "sdram_layout.h"
#include "xhgc_cart.h"

#ifndef RES_MANAGER_MAX_RECORDS
#define RES_MANAGER_MAX_RECORDS 128u
#endif

#define RES_HANDLE_INVALID_INDEX UINT16_MAX
#define RES_IMAGE_ALIGN 32u
#define RES_DATA_MAX (256u * 1024u)
#define RES_BLOB_MAX (1024u * 1024u)
#define RES_INDEX_ENVELOPE_SIZE 12u
#define XIMG_V1_HEADER_SIZE 24u

static app_arena_t s_scene_arena;
static res_record_t s_records[RES_MANAGER_MAX_RECORDS];
static uint16_t s_record_count;
static bool s_initialized;
static const char *s_last_error;
static uint32_t s_scene_arena_peak_bytes;
static uint32_t s_owner_id;
static uint32_t s_owner_generation;
static uint32_t s_session_id;
static uint32_t s_session_generation;
static uint32_t s_mount_request_id;
static bool s_mount_complete;
static bool s_mount_ready;

static res_handle_t invalid_handle(void)
{
  return (res_handle_t){ RES_HANDLE_INVALID_INDEX, 0u };
}

static uint16_t read_le16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
  return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4u) << 32);
}

static void clean_dcache_range(const void *ptr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (!ptr || size == 0u) return;
  uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)ptr + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
  (void)ptr;
  (void)size;
#endif
}

static uint32_t arena_size_to_u32(size_t size)
{
  return size > UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}

static void track_peak(void)
{
  uint32_t used = arena_size_to_u32(app_arena_used(&s_scene_arena));
  if (used > s_scene_arena_peak_bytes) s_scene_arena_peak_bytes = used;
}

static void bump_generation(res_record_t *record)
{
  if (record && ++record->generation == 0u) record->generation = 1u;
}

static void initialize_records(void)
{
  s_record_count = cart_index_count();
  memset(s_records, 0, sizeof(s_records));
  for (uint16_t i = 0u; i < s_record_count; ++i) {
    s_records[i].meta = cart_index_get(i);
    s_records[i].generation = 1u;
    s_records[i].lifetime = RES_LIFE_SCENE;
    s_records[i].state = RES_INDEXED;
  }
}

void res_manager_init(void)
{
  if (!resource_arena_claim(RESOURCE_ARENA_OWNER_RESOURCE_MANAGER)) {
    memset(&s_scene_arena, 0, sizeof(s_scene_arena));
    memset(s_records, 0, sizeof(s_records));
    s_record_count = 0u;
    s_initialized = false;
    s_last_error = "resource arena is owned by another module";
    cart_index_reset();
    return;
  }
  app_arena_init(&s_scene_arena, (void *)RESOURCE_ARENA_BASE,
                 RESOURCE_ARENA_SIZE);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_initialized = true;
  s_last_error = NULL;
  s_scene_arena_peak_bytes = 0u;
  s_owner_id = s_owner_generation = 0u;
  s_session_id = s_session_generation = 0u;
  s_mount_request_id = 0u;
  s_mount_complete = true;
  s_mount_ready = false;
  cart_index_reset();
}

bool res_manager_mount_cart(const char *cart_path)
{
  if (!s_initialized) res_manager_init();
  if (!s_initialized) return false;
  app_arena_reset(&s_scene_arena);
  if (!cart_index_load(cart_path)) {
    s_last_error = cart_index_last_error();
    return false;
  }
  initialize_records();
  s_mount_complete = s_mount_ready = true;
  s_last_error = NULL;
  return true;
}

bool res_manager_mount_cart_async(const char *cart_path,
                                  uint32_t owner_id,
                                  uint32_t owner_generation)
{
  cart_io_request_t request = {0};
  if (!s_initialized) res_manager_init();
  if (!s_initialized || !cart_path || cart_path[0] == '\0' ||
      owner_id == 0u || owner_generation == 0u || strlen(cart_path) >= 64u) {
    s_last_error = "invalid async resource session";
    return false;
  }
  app_arena_reset(&s_scene_arena);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  cart_index_reset();
  s_owner_id = owner_id;
  s_owner_generation = owner_generation;
  s_session_id = CartIoService_NextRequestId();
  if (++s_session_generation == 0u) s_session_generation = 1u;
  s_mount_request_id = CartIoService_NextRequestId();
  s_mount_complete = false;
  s_mount_ready = false;
  request.request_id = s_mount_request_id;
  request.owner_id = owner_id;
  request.operation = CART_IO_OP_RESOURCE_SESSION_OPEN;
  request.params.resource_open.session_id = s_session_id;
  request.params.resource_open.generation = s_session_generation;
  (void)snprintf(request.params.resource_open.path,
                 sizeof(request.params.resource_open.path), "%s", cart_path);
  if (!CartIoService_Submit(&request, CART_IO_TIMEOUT_CART_HEADER_MS)) {
    s_mount_complete = true;
    s_last_error = "resource session queue is full";
    return false;
  }
  s_last_error = NULL;
  return true;
}

bool res_manager_mount_complete(void) { return s_mount_complete; }
bool res_manager_mount_ready(void) { return s_mount_ready; }

static bool submit_index_read(void)
{
  cart_io_request_t request = {0};
  s_mount_request_id = CartIoService_NextRequestId();
  request.request_id = s_mount_request_id;
  request.owner_id = s_owner_id;
  request.operation = CART_IO_OP_RESOURCE_INDEX_READ;
  request.params.resource_session.session_id = s_session_id;
  request.params.resource_session.generation = s_session_generation;
  return CartIoService_Submit(&request, CART_IO_TIMEOUT_SD_READ_MS);
}

static int find_record(const cart_res_meta_t *meta)
{
  if (!meta) return -1;
  for (uint16_t i = 0u; i < s_record_count; ++i)
    if (s_records[i].meta == meta) return (int)i;
  return -1;
}

static const char *io_error(cart_io_status_t status)
{
  switch (status) {
    case CART_IO_STATUS_TIMEOUT: return "resource read timed out";
    case CART_IO_STATUS_NO_MEMORY: return "resource I/O memory exhausted";
    case CART_IO_STATUS_CORRUPT: return "resource CRC or format is corrupt";
    case CART_IO_STATUS_CANCELLED: return "resource request cancelled";
    case CART_IO_STATUS_NOT_FOUND: return "resource not found";
    default: return "resource I/O failed";
  }
}

static bool submit_blob(res_record_t *record)
{
  cart_io_request_t request = {0};
  void *buffer;
  if (!record || !record->meta || record->meta->size == 0u ||
      record->meta->size > RES_BLOB_MAX) {
    record->error = "resource blob exceeds 1 MiB staging limit";
    return false;
  }
  buffer = pvPortMalloc(record->meta->size);
  if (!buffer) {
    record->error = "resource staging allocation failed";
    return false;
  }
  record->request_id = CartIoService_NextRequestId();
  request.request_id = record->request_id;
  request.owner_id = s_owner_id;
  request.operation = CART_IO_OP_RESOURCE_BLOB_READ;
  request.params.resource_read.session_id = s_session_id;
  request.params.resource_read.generation = s_session_generation;
  request.params.resource_read.data_offset = record->meta->data_off;
  request.params.resource_read.data_size = record->meta->size;
  request.params.resource_read.expected_crc32 = record->meta->crc32;
  request.params.resource_read.output = (cart_task_buffer_t) {
    .data = buffer, .capacity = record->meta->size, .length = 0u,
    .owner_id = s_owner_id, .source = CART_BUFFER_SOURCE_RTOS_HEAP,
  };
  if (!CartIoService_Submit(&request, CART_IO_TIMEOUT_SD_READ_MS)) {
    CartTaskBuffer_Release(&request.params.resource_read.output);
    record->error = "resource request queue is full";
    return false;
  }
  return true;
}

static res_handle_t acquire(const char *path, res_lifetime_t life,
                            res_kind_t kind)
{
  const cart_res_meta_t *meta;
  int index;
  res_record_t *record;
  s_last_error = NULL;
  if (!s_initialized || !s_mount_ready || !cart_index_is_loaded()) {
    s_last_error = "cart resource index is not active";
    return invalid_handle();
  }
  if (!cart_path_is_valid(path) || !(meta = cart_index_find(path))) {
    s_last_error = "resource not found";
    return invalid_handle();
  }
  if (kind == RES_KIND_IMAGE &&
      (meta->type != XHGC_RES_IMAGE ||
       (meta->format != XHGC_IMG_BGRA8888 && meta->format != XHGC_IMG_JPEG &&
        meta->format != XHGC_IMG_JPEG_A8))) {
    s_last_error = "unsupported image resource";
    return invalid_handle();
  }
  if (kind == RES_KIND_DATA && meta->size > RES_DATA_MAX) {
    s_last_error = "resource exceeds 256 KiB limit";
    return invalid_handle();
  }
  index = find_record(meta);
  if (index < 0) {
    s_last_error = "resource record not found";
    return invalid_handle();
  }
  record = &s_records[index];
  if (record->kind != RES_KIND_NONE && record->kind != kind) {
    s_last_error = "resource is already acquired with another handle type";
    return invalid_handle();
  }
  if (record->refcount == UINT16_MAX) {
    s_last_error = "resource reference overflow";
    return invalid_handle();
  }
  ++record->refcount;
  record->kind = kind;
  record->lifetime = life;
  if (record->state == RES_READY_UNUSED) record->state = RES_READY;
  if (record->state == RES_INDEXED) {
    record->state = RES_LOADING;
    record->error = NULL;
    if (!submit_blob(record)) record->state = RES_FAILED;
  }
  return (res_handle_t){(uint16_t)index, record->generation};
}

res_handle_t res_acquire_image(const char *path, res_lifetime_t life)
{
  return acquire(path, life, RES_KIND_IMAGE);
}

res_handle_t res_acquire_data(const char *path, res_lifetime_t life)
{
  return acquire(path, life, RES_KIND_DATA);
}

static bool decode_bgra(res_record_t *record, const uint8_t *blob,
                        uint32_t blob_size)
{
  uint32_t pixel_size = (uint32_t)record->meta->width *
                        (uint32_t)record->meta->height * 4u;
  const uint8_t *pixels = blob;
  if (blob_size == pixel_size) {
    /* legacy raw BGRA8888 */
  } else if (blob_size == XIMG_V1_HEADER_SIZE + pixel_size &&
             memcmp(blob, XHGC_XIMG_MAGIC, 4u) == 0 &&
             read_le16(blob + 4u) == 1u &&
             read_le16(blob + 6u) == XIMG_V1_HEADER_SIZE &&
             read_le16(blob + 8u) == record->meta->width &&
             read_le16(blob + 10u) == record->meta->height &&
             blob[12u] == XHGC_IMG_BGRA8888 && blob[13u] == 4u &&
             read_le16(blob + 14u) == 0u &&
             read_le32(blob + 16u) == (uint32_t)record->meta->width * 4u &&
             read_le32(blob + 20u) == pixel_size) {
    pixels = blob + XIMG_V1_HEADER_SIZE;
  } else {
    record->error = "invalid BGRA8888 resource";
    return false;
  }
  void *output = app_arena_alloc(&s_scene_arena, pixel_size, RES_IMAGE_ALIGN);
  if (!output) {
    record->error = "not enough resource arena memory";
    return false;
  }
  memcpy(output, pixels, pixel_size);
  record->image = (image_resource_t) {
    .pixels = output, .size = pixel_size,
    .width = record->meta->width, .height = record->meta->height,
    .format = XHGC_IMG_BGRA8888, .crc32 = record->meta->crc32,
  };
  clean_dcache_range(output, pixel_size);
  return true;
}

static bool finish_blob(res_record_t *record, cart_task_buffer_t *buffer)
{
  bool ok = false;
  if (record->kind == RES_KIND_DATA) {
    void *bytes = app_arena_alloc(&s_scene_arena, buffer->length, 4u);
    if (bytes) {
      memcpy(bytes, buffer->data, buffer->length);
      record->data.bytes = bytes;
      record->data.size = buffer->length;
      ok = true;
    } else {
      record->error = "not enough resource arena memory";
    }
  } else if (record->meta->format == XHGC_IMG_BGRA8888) {
    ok = decode_bgra(record, (const uint8_t *)buffer->data, buffer->length);
  } else {
    uint64_t pixel_size64 = (uint64_t)record->meta->width *
                            (uint64_t)record->meta->height * 4u;
    uint32_t scratch_size = CartJpegDecoder_ScratchSize(
        record->meta->width, record->meta->height);
    app_arena_mark_t final_mark = app_arena_mark(&s_scene_arena);
    void *pixels = pixel_size64 <= UINT32_MAX
        ? app_arena_alloc(&s_scene_arena, (uint32_t)pixel_size64, RES_IMAGE_ALIGN)
        : NULL;
    app_arena_mark_t scratch_mark = app_arena_mark(&s_scene_arena);
    void *scratch = scratch_size != 0u
        ? app_arena_alloc(&s_scene_arena, scratch_size, RES_IMAGE_ALIGN) : NULL;
    track_peak();
    if (pixels && scratch && CartJpegDecoder_DecodeXimgV2(
                      buffer->data, buffer->length, record->meta->format,
                      record->meta->width, record->meta->height,
                      pixels, (uint32_t)pixel_size64, scratch, scratch_size,
                      &record->error)) {
      app_arena_reset_to(&s_scene_arena, scratch_mark);
      record->image = (image_resource_t) {
        .pixels = pixels, .size = (uint32_t)pixel_size64,
        .width = record->meta->width, .height = record->meta->height,
        .format = XHGC_IMG_BGRA8888, .crc32 = record->meta->crc32,
      };
      clean_dcache_range(pixels, (uint32_t)pixel_size64);
      ok = true;
    } else {
      app_arena_reset_to(&s_scene_arena, final_mark);
    }
    if (!pixels || !scratch) {
      record->error = "not enough resource arena memory";
    }
  }
  CartTaskBuffer_Release(buffer);
  record->state = ok ? RES_READY : RES_FAILED;
  if (ok) {
    record->error = NULL;
    track_peak();
  }
  return ok;
}

bool res_manager_handle_io_completion(const cart_io_completion_t *completion)
{
  if (!completion) return false;
  if (completion->operation == CART_IO_OP_RESOURCE_SESSION_OPEN &&
      completion->request_id == s_mount_request_id) {
    if (completion->owner_id != s_owner_id ||
        completion->status != CART_IO_STATUS_OK || !submit_index_read()) {
      s_mount_complete = true;
      s_mount_ready = false;
      s_last_error = io_error(completion->status);
    }
    return true;
  }
  if (completion->operation == CART_IO_OP_RESOURCE_INDEX_READ &&
      completion->request_id == s_mount_request_id) {
    cart_task_buffer_t buffer = completion->result.buffer;
    bool parsed = false;
    if (completion->owner_id == s_owner_id &&
        s_owner_generation != 0u &&
        completion->status == CART_IO_STATUS_OK &&
        buffer.data && buffer.length >= RES_INDEX_ENVELOPE_SIZE) {
      const uint8_t *bytes = (const uint8_t *)buffer.data;
      parsed = cart_index_parse(bytes + RES_INDEX_ENVELOPE_SIZE,
                                buffer.length - RES_INDEX_ENVELOPE_SIZE,
                                read_le64(bytes), read_le32(bytes + 8u));
      if (parsed) initialize_records();
    }
    CartTaskBuffer_Release(&buffer);
    s_mount_complete = true;
    s_mount_ready = parsed;
    s_last_error = parsed ? NULL : (completion->status == CART_IO_STATUS_OK
        ? cart_index_last_error() : io_error(completion->status));
    return true;
  }
  if (completion->operation == CART_IO_OP_RESOURCE_BLOB_READ) {
    cart_task_buffer_t buffer = completion->result.buffer;
    for (uint16_t i = 0u; i < s_record_count; ++i) {
      res_record_t *record = &s_records[i];
      if (record->request_id != completion->request_id) continue;
      record->request_id = 0u;
      if (completion->owner_id != s_owner_id ||
          record->state != RES_LOADING ||
          completion->status != CART_IO_STATUS_OK) {
        CartTaskBuffer_Release(&buffer);
        record->state = RES_FAILED;
        record->error = io_error(completion->status);
      } else {
        (void)finish_blob(record, &buffer);
      }
      return true;
    }
    CartTaskBuffer_Release(&buffer);
    return true;
  }
  return completion->operation == CART_IO_OP_RESOURCE_SESSION_CLOSE;
}

void *res_alloc_image_view_buffer(size_t size, size_t align)
{
  void *pixels = app_arena_alloc(&s_scene_arena, size, align);
  if (!pixels) s_last_error = "not enough app arena memory for image view";
  else track_peak();
  return pixels;
}

bool res_handle_valid(res_handle_t handle)
{
  if (handle.index >= s_record_count ||
      s_records[handle.index].generation != handle.generation) return false;
  return s_records[handle.index].state != RES_INDEXED;
}

res_state_t res_handle_state(res_handle_t handle)
{
  return res_handle_valid(handle) ? s_records[handle.index].state : RES_FAILED;
}

const char *res_handle_error(res_handle_t handle)
{
  return res_handle_valid(handle) ? s_records[handle.index].error
                                  : "invalid resource handle";
}

const image_resource_t *res_get_image(res_handle_t handle)
{
  if (!res_handle_valid(handle) ||
      (s_records[handle.index].state != RES_READY &&
       s_records[handle.index].state != RES_READY_UNUSED) ||
      s_records[handle.index].kind != RES_KIND_IMAGE) return NULL;
  return &s_records[handle.index].image;
}

const data_resource_t *res_get_data(res_handle_t handle)
{
  if (!res_handle_valid(handle) ||
      (s_records[handle.index].state != RES_READY &&
       s_records[handle.index].state != RES_READY_UNUSED) ||
      s_records[handle.index].kind != RES_KIND_DATA) return NULL;
  return &s_records[handle.index].data;
}

bool res_get_image_dimensions(res_handle_t handle, uint16_t *width,
                              uint16_t *height)
{
  if (!width || !height || !res_handle_valid(handle) ||
      s_records[handle.index].kind != RES_KIND_IMAGE ||
      !s_records[handle.index].meta) return false;
  *width = s_records[handle.index].meta->width;
  *height = s_records[handle.index].meta->height;
  return *width != 0u && *height != 0u;
}

uint32_t res_get_storage_size(res_handle_t handle)
{
  return res_handle_valid(handle) && s_records[handle.index].meta
      ? s_records[handle.index].meta->size : 0u;
}

void res_release(res_handle_t handle)
{
  if (!res_handle_valid(handle)) return;
  res_record_t *record = &s_records[handle.index];
  if (record->refcount > 0u) --record->refcount;
  if (record->refcount == 0u && record->state == RES_READY)
    record->state = RES_READY_UNUSED;
}

bool res_retain(res_handle_t handle)
{
  if (!res_handle_valid(handle)) return false;
  res_record_t *record = &s_records[handle.index];
  if (record->refcount == UINT16_MAX) return false;
  ++record->refcount;
  if (record->state == RES_READY_UNUSED) record->state = RES_READY;
  return true;
}

void res_scene_reset(void)
{
  if (!s_initialized) return;
  if (s_session_id != 0u) {
    cart_io_request_t close = {0};
    close.request_id = CartIoService_NextRequestId();
    close.owner_id = 0u;
    close.operation = CART_IO_OP_RESOURCE_SESSION_CLOSE;
    close.params.resource_session.session_id = s_session_id;
    close.params.resource_session.generation = s_session_generation;
    (void)CartIoService_Submit(&close, CART_IO_TIMEOUT_CART_HEADER_MS);
  }
  if (s_owner_id != 0u) (void)CartIoService_CancelOwner(s_owner_id);
  app_arena_reset(&s_scene_arena);
  for (uint16_t i = 0u; i < s_record_count; ++i) bump_generation(&s_records[i]);
  memset(&s_scene_arena, 0, sizeof(s_scene_arena));
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_initialized = false;
  s_last_error = NULL;
  s_scene_arena_peak_bytes = 0u;
  s_owner_id = s_owner_generation = 0u;
  s_session_id = s_session_generation = 0u;
  s_mount_request_id = 0u;
  s_mount_complete = true;
  s_mount_ready = false;
  cart_index_reset();
  (void)resource_arena_release(RESOURCE_ARENA_OWNER_RESOURCE_MANAGER);
}

const char *res_last_error(void) { return s_last_error; }
uint32_t res_manager_used_bytes(void) { return arena_size_to_u32(app_arena_used(&s_scene_arena)); }
uint32_t res_manager_peak_bytes(void) { return s_scene_arena_peak_bytes; }
uint32_t res_manager_capacity_bytes(void) { return (uint32_t)RESOURCE_ARENA_SIZE; }
uint32_t res_manager_indexed_count(void) { return s_record_count; }
uint32_t res_manager_refcount_anomaly_count(void) { return 0u; }

uint32_t res_manager_alive_count(void)
{
  uint32_t count = 0u;
  for (uint16_t i = 0u; i < s_record_count; ++i)
    if (s_records[i].state == RES_READY ||
        s_records[i].state == RES_READY_UNUSED) ++count;
  return count;
}
