#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_arena.h"
#include "cart_io_service.h"
#include "cart_jpeg_decoder.h"
#include "resource_arena_owner.h"
#include "resource_manager.h"
#include "sdram_layout.h"
#include "xhgc_cart.h"

uint8_t g_host_resource_arena[RESOURCE_ARENA_SIZE];

static cart_io_request_t s_requests[16];
static size_t s_request_count;
static uint32_t s_next_request_id = 1u;
static uint32_t s_cancelled_owner;
static size_t s_release_count;
static bool s_index_loaded;

static cart_res_meta_t s_meta[] = {
  { .data_off = 0u, .size = 8u, .crc32 = 1u, .type = XHGC_RES_IMAGE,
    .format = XHGC_IMG_BGRA8888, .width = 2u, .height = 1u,
    .path = "assets/image.raw" },
  { .data_off = 8u, .size = 4u, .crc32 = 2u, .type = 0u,
    .format = XHGC_IMG_NONE, .path = "assets/data.bin" },
  { .data_off = 12u, .size = 256u * 1024u, .crc32 = 3u, .type = 0u,
    .format = XHGC_IMG_NONE, .path = "assets/max.bin" },
  { .data_off = 12u + 256u * 1024u, .size = 256u * 1024u + 1u,
    .crc32 = 4u, .type = 0u, .format = XHGC_IMG_NONE,
    .path = "assets/too-big.bin" },
};

void *pvPortMalloc(size_t size) { return malloc(size); }
void vPortFree(void *pointer) { free(pointer); }

void app_arena_init(app_arena_t *arena, void *base, size_t size)
{
  arena->base = base;
  arena->ptr = base;
  arena->end = (uint8_t *)base + size;
}

void *app_arena_alloc(app_arena_t *arena, size_t size, size_t align)
{
  uintptr_t current = (uintptr_t)arena->ptr;
  uintptr_t aligned = (current + align - 1u) & ~(uintptr_t)(align - 1u);
  if (size == 0u || align == 0u || (align & (align - 1u)) != 0u ||
      aligned > (uintptr_t)arena->end ||
      size > (size_t)((uintptr_t)arena->end - aligned)) return NULL;
  arena->ptr = (uint8_t *)(aligned + size);
  return (void *)aligned;
}

app_arena_mark_t app_arena_mark(app_arena_t *arena) { return arena->ptr; }

void app_arena_reset_to(app_arena_t *arena, app_arena_mark_t mark)
{
  if (mark >= arena->base && mark <= arena->end) arena->ptr = mark;
}

void app_arena_reset(app_arena_t *arena) { arena->ptr = arena->base; }

size_t app_arena_used(const app_arena_t *arena)
{
  return arena && arena->base && arena->ptr
      ? (size_t)(arena->ptr - arena->base) : 0u;
}

size_t app_arena_remaining(const app_arena_t *arena)
{
  return arena && arena->ptr && arena->end
      ? (size_t)(arena->end - arena->ptr) : 0u;
}

bool resource_arena_claim(resource_arena_owner_t owner)
{
  return owner == RESOURCE_ARENA_OWNER_RESOURCE_MANAGER;
}

bool resource_arena_release(resource_arena_owner_t owner)
{
  return owner == RESOURCE_ARENA_OWNER_RESOURCE_MANAGER;
}

resource_arena_owner_t resource_arena_get_owner(void)
{
  return RESOURCE_ARENA_OWNER_RESOURCE_MANAGER;
}

const char *resource_arena_owner_name(resource_arena_owner_t owner)
{
  (void)owner;
  return "host";
}

uint32_t CartIoService_NextRequestId(void) { return s_next_request_id++; }

bool CartIoService_Submit(const cart_io_request_t *request, uint32_t timeout_ms)
{
  (void)timeout_ms;
  assert(s_request_count < sizeof(s_requests) / sizeof(s_requests[0]));
  s_requests[s_request_count++] = *request;
  return true;
}

bool CartIoService_CancelOwner(uint32_t owner_id)
{
  s_cancelled_owner = owner_id;
  return true;
}

void CartTaskBuffer_Release(cart_task_buffer_t *buffer)
{
  if (buffer && buffer->data && buffer->source == CART_BUFFER_SOURCE_RTOS_HEAP) {
    free(buffer->data);
    ++s_release_count;
  }
  if (buffer) memset(buffer, 0, sizeof(*buffer));
}

void cart_index_reset(void) { s_index_loaded = false; }

bool cart_index_parse(const void *bytes, uint32_t size,
                      uint64_t data_offset, uint32_t data_size)
{
  (void)bytes;
  (void)data_offset;
  s_index_loaded = size != 0u && data_size >= 256u * 1024u + 13u;
  return s_index_loaded;
}

bool cart_index_load(const char *path) { (void)path; return false; }
bool cart_index_is_loaded(void) { return s_index_loaded; }
uint16_t cart_index_count(void) { return (uint16_t)(sizeof(s_meta) / sizeof(s_meta[0])); }
const cart_res_meta_t *cart_index_get(uint16_t index)
{
  return index < cart_index_count() ? &s_meta[index] : NULL;
}

const cart_res_meta_t *cart_index_find(const char *path)
{
  for (uint16_t i = 0u; i < cart_index_count(); ++i)
    if (strcmp(path, s_meta[i].path) == 0) return &s_meta[i];
  return NULL;
}

const char *cart_index_last_error(void) { return "host index error"; }

bool cart_path_is_valid(const char *path)
{
  return path && path[0] != '\0' && path[0] != '/' && strstr(path, "..") == NULL;
}

bool cart_read_data(uint32_t offset, void *dst, uint32_t size)
{
  (void)offset; (void)dst; (void)size;
  return false;
}

uint32_t CartJpegDecoder_ScratchSize(uint16_t width, uint16_t height)
{
  return (uint32_t)width * height * 3u;
}

bool CartJpegDecoder_DecodeXimgV2(const void *blob, uint32_t blob_size,
                                  uint8_t storage_format,
                                  uint16_t expected_width,
                                  uint16_t expected_height,
                                  void *output_bgra, uint32_t output_size,
                                  void *scratch_ycbcr, uint32_t scratch_size,
                                  const char **error)
{
  (void)blob; (void)blob_size; (void)storage_format;
  (void)expected_width; (void)expected_height; (void)output_bgra;
  (void)output_size; (void)scratch_ycbcr; (void)scratch_size;
  if (error) *error = "not used by BGRA host test";
  return false;
}

static cart_io_completion_t completion_for(const cart_io_request_t *request,
                                           cart_io_status_t status)
{
  cart_io_completion_t completion = {
    .request_id = request->request_id,
    .owner_id = request->owner_id,
    .operation = request->operation,
    .status = status,
  };
  return completion;
}

static void finish_mount(void)
{
  assert(res_manager_mount_cart_async("0:/apps/test.cart", 77u, 9u));
  assert(s_request_count == 1u);
  assert(s_requests[0].operation == CART_IO_OP_RESOURCE_SESSION_OPEN);
  cart_io_completion_t opened = completion_for(&s_requests[0], CART_IO_STATUS_OK);
  assert(res_manager_handle_io_completion(&opened));
  assert(s_request_count == 2u);
  assert(s_requests[1].operation == CART_IO_OP_RESOURCE_INDEX_READ);

  uint8_t *index = malloc(13u);
  assert(index);
  memset(index, 0, 13u);
  index[8] = 13u;
  index[9] = 0u;
  index[10] = 4u;
  cart_io_completion_t indexed = completion_for(&s_requests[1], CART_IO_STATUS_OK);
  indexed.result.buffer = (cart_task_buffer_t) {
    .data = index, .capacity = 13u, .length = 13u, .owner_id = 77u,
    .source = CART_BUFFER_SOURCE_RTOS_HEAP,
  };
  assert(res_manager_handle_io_completion(&indexed));
  assert(res_manager_mount_complete());
  assert(res_manager_mount_ready());
  assert(res_manager_indexed_count() == cart_index_count());
}

static void test_image_dedup_ready_and_refcount(void)
{
  res_handle_t first = res_acquire_image("assets/image.raw", RES_LIFE_SCENE);
  assert(res_handle_valid(first));
  assert(res_handle_state(first) == RES_LOADING);
  assert(s_request_count == 3u);
  res_handle_t second = res_acquire_image("assets/image.raw", RES_LIFE_SCENE);
  assert(first.index == second.index && first.generation == second.generation);
  assert(s_request_count == 3u);

  cart_io_request_t *read = &s_requests[2];
  assert(read->operation == CART_IO_OP_RESOURCE_BLOB_READ);
  uint8_t expected[8] = {1u, 2u, 3u, 255u, 4u, 5u, 6u, 128u};
  memcpy(read->params.resource_read.output.data, expected, sizeof(expected));
  cart_io_completion_t ready = completion_for(read, CART_IO_STATUS_OK);
  ready.result.buffer = read->params.resource_read.output;
  ready.result.buffer.length = sizeof(expected);
  assert(res_manager_handle_io_completion(&ready));
  assert(res_handle_state(first) == RES_READY);
  const image_resource_t *image = res_get_image(first);
  assert(image && image->size == sizeof(expected));
  assert(memcmp(image->pixels, expected, sizeof(expected)) == 0);
  res_release(first);
  assert(res_handle_state(second) == RES_READY);
  res_release(second);
  assert(res_handle_state(second) == RES_READY_UNUSED);
  assert(res_retain(second));
  assert(res_handle_state(second) == RES_READY);
}

static void test_data_ready_failed_and_limit(void)
{
  res_handle_t data = res_acquire_data("assets/data.bin", RES_LIFE_SCENE);
  assert(res_handle_state(data) == RES_LOADING);
  cart_io_request_t *read = &s_requests[3];
  memcpy(read->params.resource_read.output.data, "DATA", 4u);
  cart_io_completion_t ready = completion_for(read, CART_IO_STATUS_OK);
  ready.result.buffer = read->params.resource_read.output;
  ready.result.buffer.length = 4u;
  assert(res_manager_handle_io_completion(&ready));
  const data_resource_t *result = res_get_data(data);
  assert(result && result->size == 4u && memcmp(result->bytes, "DATA", 4u) == 0);

  res_handle_t max = res_acquire_data("assets/max.bin", RES_LIFE_SCENE);
  assert(res_handle_valid(max));
  assert(res_handle_state(max) == RES_LOADING);
  cart_io_request_t *max_read = &s_requests[4];
  cart_io_completion_t failed = completion_for(max_read, CART_IO_STATUS_IO_ERROR);
  failed.result.buffer = max_read->params.resource_read.output;
  assert(res_manager_handle_io_completion(&failed));
  assert(res_handle_state(max) == RES_FAILED);
  assert(strcmp(res_handle_error(max), "resource I/O failed") == 0);

  res_handle_t too_big = res_acquire_data("assets/too-big.bin", RES_LIFE_SCENE);
  assert(!res_handle_valid(too_big));
  assert(strcmp(res_last_error(), "resource exceeds 256 KiB limit") == 0);
}

static void test_cancel_stale_and_scene_reset(void)
{
  res_scene_reset();
  assert(s_cancelled_owner == 77u);
  assert(s_request_count == 6u);
  assert(s_requests[5].operation == CART_IO_OP_RESOURCE_SESSION_CLOSE);

  s_request_count = 0u;
  finish_mount();
  res_handle_t pending = res_acquire_data("assets/data.bin", RES_LIFE_SCENE);
  assert(res_handle_state(pending) == RES_LOADING);
  cart_io_request_t pending_read = s_requests[s_request_count - 1u];
  size_t releases_before = s_release_count;
  res_scene_reset();
  assert(!res_handle_valid(pending));
  cart_io_completion_t stale = completion_for(&pending_read, CART_IO_STATUS_CANCELLED);
  stale.result.buffer = pending_read.params.resource_read.output;
  assert(res_manager_handle_io_completion(&stale));
  assert(s_release_count == releases_before + 1u);
}

int main(void)
{
  res_manager_init();
  finish_mount();
  test_image_dedup_ready_and_refcount();
  test_data_ready_failed_and_limit();
  test_cancel_stale_and_scene_reset();
  puts("resource_manager_test: PASS");
  return 0;
}
