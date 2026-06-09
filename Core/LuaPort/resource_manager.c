#include "resource_manager.h"

#include <stddef.h>
#include <string.h>

#include "app_arena.h"
#include "main.h"
#include "sdram_layout.h"
#include "xhgc_cart.h"

#ifndef RES_MANAGER_MAX_RECORDS
#define RES_MANAGER_MAX_RECORDS 128u
#endif

#define RES_HANDLE_INVALID_INDEX UINT16_MAX
#define RES_IMAGE_ALIGN 32u

static app_arena_t s_scene_arena;
static res_record_t s_records[RES_MANAGER_MAX_RECORDS];
static uint16_t s_record_count;
static bool s_initialized;
static const char *s_last_error;

static res_handle_t invalid_handle(void)
{
  res_handle_t h = { RES_HANDLE_INVALID_INDEX, 0u };
  return h;
}

static void bump_generation(res_record_t *rec)
{
  if (!rec) return;
  rec->generation++;
  if (rec->generation == 0u) rec->generation = 1u;
}

static void clean_dcache_range(const void *ptr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (!ptr || size == 0u) return;
  uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)ptr + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
  (void)ptr;
  (void)size;
#endif
}

void res_manager_init(void)
{
  app_arena_init(&s_scene_arena, (void*)RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_initialized = true;
  s_last_error = NULL;
  cart_index_reset();
}

bool res_manager_mount_cart(const char *cart_path)
{
  if (!s_initialized) res_manager_init();
  app_arena_reset(&s_scene_arena);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;

  if (!cart_index_load(cart_path)) {
    s_last_error = cart_index_last_error();
    return false;
  }

  s_record_count = cart_index_count();
  if (s_record_count > RES_MANAGER_MAX_RECORDS) {
    s_last_error = "too many cart resources";
    return false;
  }

  for (uint16_t i = 0; i < s_record_count; ++i) {
    s_records[i].meta = cart_index_get(i);
    s_records[i].generation = 1u;
    s_records[i].lifetime = RES_LIFE_SCENE;
    s_records[i].state = RES_INDEXED;
  }

  s_last_error = NULL;
  return true;
}

static int find_record(const cart_res_meta_t *meta)
{
  if (!meta) return -1;
  for (uint16_t i = 0; i < s_record_count; ++i) {
    if (s_records[i].meta == meta) return (int)i;
  }
  return -1;
}

res_handle_t res_acquire_image(const char *path, res_lifetime_t life)
{
  const cart_res_meta_t *meta;
  int index;
  res_record_t *rec;
  app_arena_mark_t mark;
  void *pixels;

  s_last_error = NULL;
  if (!s_initialized) res_manager_init();
  if (!cart_index_is_loaded()) {
    s_last_error = "cart resource index is not active";
    return invalid_handle();
  }
  if (!cart_path_is_valid(path)) {
    s_last_error = "invalid cart resource path";
    return invalid_handle();
  }

  meta = cart_index_find(path);
  if (!meta) {
    s_last_error = "resource not found";
    return invalid_handle();
  }
  if (meta->type != XHGC_RES_IMAGE) {
    s_last_error = "unsupported resource type";
    return invalid_handle();
  }
  if (meta->format != XHGC_IMG_BGRA8888) {
    s_last_error = "unsupported image format";
    return invalid_handle();
  }

  index = find_record(meta);
  if (index < 0) {
    s_last_error = "resource record not found";
    return invalid_handle();
  }
  rec = &s_records[index];

  if (rec->state == RES_READY || rec->state == RES_READY_UNUSED) {
    if (rec->refcount == UINT16_MAX) {
      s_last_error = "resource reference overflow";
      return invalid_handle();
    }
    rec->refcount++;
    rec->state = RES_READY;
    rec->lifetime = life;
    return (res_handle_t){ (uint16_t)index, rec->generation };
  }

  mark = app_arena_mark(&s_scene_arena);
  rec->state = RES_LOADING;
  pixels = app_arena_alloc(&s_scene_arena, meta->size, RES_IMAGE_ALIGN);
  if (!pixels) {
    rec->state = RES_INDEXED;
    s_last_error = "not enough app arena memory";
    return invalid_handle();
  }
  if (!cart_read_data(meta->data_off, pixels, meta->size)) {
    app_arena_reset_to(&s_scene_arena, mark);
    memset(&rec->image, 0, sizeof(rec->image));
    rec->refcount = 0u;
    rec->state = RES_FAILED;
    s_last_error = "cart read failed";
    return invalid_handle();
  }

  clean_dcache_range(pixels, meta->size);
  rec->image.pixels = pixels;
  rec->image.size = meta->size;
  rec->image.width = meta->width;
  rec->image.height = meta->height;
  rec->image.format = meta->format;
  rec->image.crc32 = meta->crc32;
  rec->refcount = 1u;
  rec->lifetime = life;
  rec->state = RES_READY;
  return (res_handle_t){ (uint16_t)index, rec->generation };
}

void *res_alloc_image_view_buffer(size_t size, size_t align)
{
  void *pixels;

  s_last_error = NULL;
  if (!s_initialized) res_manager_init();

  pixels = app_arena_alloc(&s_scene_arena, size, align);
  if (!pixels) {
    s_last_error = "not enough app arena memory for image view";
  }
  return pixels;
}

bool res_handle_valid(res_handle_t h)
{
  if (h.index >= s_record_count) return false;
  if (s_records[h.index].generation != h.generation) return false;
  return s_records[h.index].state == RES_READY ||
         s_records[h.index].state == RES_READY_UNUSED;
}

const image_resource_t *res_get_image(res_handle_t h)
{
  if (!res_handle_valid(h)) return NULL;
  return &s_records[h.index].image;
}

void res_release(res_handle_t h)
{
  res_record_t *rec;
  if (!res_handle_valid(h)) return;
  rec = &s_records[h.index];
  if (rec->refcount > 0u) rec->refcount--;
  if (rec->refcount == 0u) rec->state = RES_READY_UNUSED;
}

void res_scene_reset(void)
{
  if (!s_initialized) res_manager_init();
  app_arena_reset(&s_scene_arena);
  for (uint16_t i = 0; i < s_record_count; ++i) {
    res_record_t *rec = &s_records[i];
    if (rec->lifetime != RES_LIFE_SCENE) continue;
    memset(&rec->image, 0, sizeof(rec->image));
    rec->refcount = 0u;
    rec->lifetime = RES_LIFE_SCENE;
    rec->state = rec->meta ? RES_INDEXED : RES_FAILED;
    bump_generation(rec);
  }
}

const char *res_last_error(void)
{
  return s_last_error;
}
