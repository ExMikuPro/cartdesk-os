#include "cart_index.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "main.h"
#include "xhgc_cart.h"

#ifndef CART_INDEX_MAX_RESOURCES
#define CART_INDEX_MAX_RESOURCES 128u
#endif

#ifndef CART_INDEX_PATH_MAX
#define CART_INDEX_PATH_MAX 256u
#endif

#ifndef CART_INDEX_SD_DRIVE
#define CART_INDEX_SD_DRIVE "0:"
#endif

#ifndef CART_DATA_BOUNCE_SIZE
#define CART_DATA_BOUNCE_SIZE 4096u
#endif

#if defined(__APPLE__)
#define CART_INDEX_RAM_RUNTIME __attribute__((aligned(32)))
#else
#define CART_INDEX_RAM_RUNTIME __attribute__((section(".ram_runtime"), aligned(32)))
#endif

#define CART_INDEX_HEADER_SIZE 32u

static cart_res_meta_t s_meta[CART_INDEX_MAX_RESOURCES];
static char s_paths[CART_INDEX_MAX_RESOURCES][CART_INDEX_PATH_MAX];
static uint8_t s_bounce[CART_DATA_BOUNCE_SIZE] CART_INDEX_RAM_RUNTIME;
static uint16_t s_count;
static char s_cart_path[256];
static uint64_t s_data_offset;
static uint32_t s_data_size;
static bool s_loaded;
static const char *s_last_error;

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

static uint32_t fnv1a32(const char *s)
{
  uint32_t hash = 2166136261u;
  while (s && *s) {
    hash ^= (uint8_t)*s++;
    hash *= 16777619u;
  }
  return hash;
}

static bool add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
  if (UINT32_MAX - a < b) return true;
  if (out) *out = a + b;
  return false;
}

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out)
{
  if (UINT64_MAX - a < b) return true;
  if (out) *out = a + b;
  return false;
}

static void make_sd_path(char *out, size_t out_size, const char *path)
{
  if (!out || out_size == 0u) return;
  if (!path) {
    out[0] = '\0';
  } else if (strchr(path, ':')) {
    snprintf(out, out_size, "%s", path);
  } else if (path[0] == '/') {
    snprintf(out, out_size, "%s%s", CART_INDEX_SD_DRIVE, path);
  } else {
    snprintf(out, out_size, "%s/%s", CART_INDEX_SD_DRIVE, path);
  }
}

static bool cart_read_abs(const XHGC_Cart *cart, uint64_t offset, void *dst, uint32_t size)
{
  uint64_t end = 0;
  if (!cart || !cart->read || !dst) return false;
  if (add_overflow_u64(offset, size, &end) || end > cart->image_size) return false;
  return cart->read(cart->reader_ctx, offset, dst, size) == 0;
}

bool cart_path_is_valid(const char *path)
{
  const char *seg = path;

  if (!path || path[0] == '\0') return false;
  if (path[0] == '/') return false;

  for (const char *p = path; *p; ++p) {
    if (*p == '\\') return false;
    if (*p == ':') return false;
    if (*p == '/') {
      if (p == seg) return false;
      if ((p - seg) == 2 && seg[0] == '.' && seg[1] == '.') return false;
      seg = p + 1;
    }
  }

  if (seg[0] == '\0') return false;
  if (seg[0] == '.' && seg[1] == '.' && seg[2] == '\0') return false;
  return true;
}

static bool read_index_string(const XHGC_Cart *cart,
                              const XHGC_CartSlot *index_slot,
                              uint32_t strings_off,
                              uint32_t strings_size,
                              uint32_t path_off,
                              char *out,
                              uint32_t out_size)
{
  uint32_t i = 0;
  uint64_t base = 0;
  uint8_t ch = 0u;

  if (!cart || !index_slot || !out || out_size == 0u) return false;
  out[0] = '\0';
  if (path_off >= strings_size) return false;
  if (add_overflow_u64(index_slot->offset, strings_off, &base)) return false;

  while (path_off + i < strings_size) {
    if (!cart_read_abs(cart, base + path_off + i, &ch, 1u)) return false;
    if (ch == 0u) {
      out[i] = '\0';
      return cart_path_is_valid(out);
    }
    if (i + 1u >= out_size) return false;
    out[i++] = (char)ch;
  }

  return false;
}

void cart_index_reset(void)
{
  memset(s_meta, 0, sizeof(s_meta));
  memset(s_paths, 0, sizeof(s_paths));
  s_count = 0u;
  s_cart_path[0] = '\0';
  s_data_offset = 0u;
  s_data_size = 0u;
  s_loaded = false;
  s_last_error = NULL;
}

bool cart_index_load(const char *cart_path)
{
  char fatfs_path[256];
  XHGC_CartFatFs cart_file;
  XHGC_CartSlot index_slot;
  XHGC_CartSlot data_slot;
  uint8_t header[CART_INDEX_HEADER_SIZE];
  uint16_t version;
  uint16_t entry_size;
  uint32_t count;
  uint32_t entries_off;
  uint32_t strings_off;
  uint32_t strings_size;
  uint32_t flags;
  uint64_t entries_end;
  int rc;

  cart_index_reset();
  if (!cart_path || cart_path[0] == '\0') {
    s_last_error = "cart path is required";
    return false;
  }

  (void)SD_FATFS_Mount();
  make_sd_path(fatfs_path, sizeof(fatfs_path), cart_path);
  rc = xhgc_cart_open_fatfs(&cart_file, fatfs_path);
  if (rc != XHGC_CART_OK) {
    s_last_error = "failed to open cart";
    return false;
  }

  rc = xhgc_cart_get_slot(&cart_file.cart, XHGC_CART_SLOT_INDEX, &index_slot);
  if (rc == XHGC_CART_OK) rc = xhgc_cart_get_slot(&cart_file.cart, XHGC_CART_SLOT_DATA, &data_slot);
  if (rc != XHGC_CART_OK || index_slot.size < CART_INDEX_HEADER_SIZE) {
    xhgc_cart_close_fatfs(&cart_file);
    s_last_error = "missing cart index/data segment";
    return false;
  }

  if (!cart_read_abs(&cart_file.cart, index_slot.offset, header, sizeof(header))) {
    xhgc_cart_close_fatfs(&cart_file);
    s_last_error = "failed to read cart index";
    return false;
  }
  if (memcmp(header, XHGC_INDEX_MAGIC, XHGC_INDEX_MAGIC_SIZE) != 0) {
    xhgc_cart_close_fatfs(&cart_file);
    s_last_error = "invalid cart index magic";
    return false;
  }

  version = read_le16(header + 8u);
  entry_size = read_le16(header + 10u);
  count = read_le32(header + 12u);
  entries_off = read_le32(header + 16u);
  strings_off = read_le32(header + 20u);
  strings_size = read_le32(header + 24u);
  flags = read_le32(header + 28u);

  entries_end = (uint64_t)entries_off + (uint64_t)count * XHGC_INDEX_ENTRY_SIZE;
  if (version != XHGC_INDEX_VERSION || entry_size != XHGC_INDEX_ENTRY_SIZE || flags != 0u ||
      entries_off < CART_INDEX_HEADER_SIZE || entries_end > index_slot.size ||
      strings_off < entries_end || (uint64_t)strings_off + strings_size > index_slot.size ||
      count > CART_INDEX_MAX_RESOURCES) {
    xhgc_cart_close_fatfs(&cart_file);
    s_last_error = "invalid cart index";
    return false;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint8_t ent[XHGC_INDEX_ENTRY_SIZE];
    uint32_t path_off;
    uint32_t data_end;
    uint16_t flags16;
    uint32_t reserved;
    cart_res_meta_t *meta = &s_meta[i];
    uint64_t ent_offset = index_slot.offset + entries_off + (uint64_t)i * XHGC_INDEX_ENTRY_SIZE;

    if (!cart_read_abs(&cart_file.cart, ent_offset, ent, sizeof(ent))) {
      xhgc_cart_close_fatfs(&cart_file);
      s_last_error = "failed to read cart index entry";
      return false;
    }

    meta->path_hash = read_le32(ent);
    path_off = read_le32(ent + 4u);
    meta->data_off = read_le32(ent + 8u);
    meta->size = read_le32(ent + 12u);
    meta->crc32 = read_le32(ent + 16u);
    meta->type = ent[20];
    meta->format = ent[21];
    meta->width = read_le16(ent + 22u);
    meta->height = read_le16(ent + 24u);
    flags16 = read_le16(ent + 26u);
    reserved = read_le32(ent + 28u);

    if (flags16 != 0u || reserved != 0u ||
        add_overflow_u32(meta->data_off, meta->size, &data_end) ||
        data_end > data_slot.size ||
        !read_index_string(&cart_file.cart, &index_slot, strings_off, strings_size,
                           path_off, s_paths[i], sizeof(s_paths[i])) ||
        meta->path_hash != fnv1a32(s_paths[i])) {
      xhgc_cart_close_fatfs(&cart_file);
      s_last_error = "invalid cart index entry";
      return false;
    }
    meta->path = s_paths[i];
  }

  snprintf(s_cart_path, sizeof(s_cart_path), "%s", cart_path);
  s_data_offset = data_slot.offset;
  s_data_size = data_slot.size;
  s_count = (uint16_t)count;
  s_loaded = true;
  xhgc_cart_close_fatfs(&cart_file);
  return true;
}

bool cart_index_is_loaded(void)
{
  return s_loaded;
}

const cart_res_meta_t *cart_index_find(const char *path)
{
  uint32_t hash;
  if (!s_loaded || !cart_path_is_valid(path)) return NULL;
  hash = fnv1a32(path);
  for (uint16_t i = 0; i < s_count; ++i) {
    if (s_meta[i].path_hash == hash && strcmp(s_meta[i].path, path) == 0) {
      return &s_meta[i];
    }
  }
  return NULL;
}

const cart_res_meta_t *cart_index_get(uint16_t index)
{
  if (!s_loaded || index >= s_count) return NULL;
  return &s_meta[index];
}

uint16_t cart_index_count(void)
{
  return s_loaded ? s_count : 0u;
}

const char *cart_index_last_error(void)
{
  return s_last_error;
}

bool cart_read_data(uint32_t data_off, void *dst, uint32_t size)
{
  char fatfs_path[256];
  XHGC_CartFatFs cart_file;
  XHGC_CartFile file;
  uint64_t image_offset = 0u;
  uint32_t copied = 0u;
  bool ok;

  if (!s_loaded || !dst) return false;
  if ((uint64_t)data_off + size > s_data_size) {
    return false;
  }
  if (add_overflow_u64(s_data_offset, data_off, &image_offset)) {
    return false;
  }

  (void)SD_FATFS_Mount();
  make_sd_path(fatfs_path, sizeof(fatfs_path), s_cart_path);
  if (xhgc_cart_open_fatfs(&cart_file, fatfs_path) != XHGC_CART_OK) {
    return false;
  }

  file.image_offset = image_offset;
  file.data_offset = data_off;
  file.data_size = size;
  file.crc32 = 0u;
  ok = true;
  while (copied < size) {
    uint32_t want = size - copied;
    if (want > sizeof(s_bounce)) want = sizeof(s_bounce);

    if (xhgc_cart_read_file(&cart_file.cart, &file, copied, s_bounce, want) != XHGC_CART_OK) {
      ok = false;
      break;
    }
    memcpy((uint8_t*)dst + copied, s_bounce, want);
    copied += want;
  }
  xhgc_cart_close_fatfs(&cart_file);
  return ok;
}
