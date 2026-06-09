#include "lua_cart_resource_cache.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "main.h"
#include "resource_arena_owner.h"
#include "sdram_layout.h"

#ifndef XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE
#define XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE 0
#endif

#if XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE

#ifndef LUA_CART_RES_CACHE_MAX_ENTRIES
#define LUA_CART_RES_CACHE_MAX_ENTRIES 64u
#endif

#ifndef LUA_CART_RES_CACHE_MAX_BLOCKS
#define LUA_CART_RES_CACHE_MAX_BLOCKS 96u
#endif

#ifndef LUA_CART_RES_CACHE_BOUNCE_SIZE
#define LUA_CART_RES_CACHE_BOUNCE_SIZE 4096u
#endif

#ifndef LUA_CART_RES_CACHE_SD_DRIVE
#define LUA_CART_RES_CACHE_SD_DRIVE "0:"
#endif

#define LUA_CART_RES_PATH_MAX 256u
#define LUA_CART_RES_ALIGN 32u

#if defined(__APPLE__)
#define LUA_CART_RES_RAM_RUNTIME __attribute__((aligned(32)))
#else
#define LUA_CART_RES_RAM_RUNTIME __attribute__((section(".ram_runtime"), aligned(32)))
#endif

typedef struct {
  uint32_t offset;
  uint32_t size;
  uint8_t free;
} LuaCartArenaBlock;

typedef struct {
  char path[LUA_CART_RES_PATH_MAX];
  XhgcIndexEntry entry;
  uint8_t listed;
  uint8_t loaded;
  uint16_t refs;
  uint32_t offset;
  uint32_t size;
} LuaCartResourceEntry;

static LuaCartResourceEntry s_entries[LUA_CART_RES_CACHE_MAX_ENTRIES];
static uint32_t s_entry_count;
static LuaCartArenaBlock s_blocks[LUA_CART_RES_CACHE_MAX_BLOCKS];
static uint32_t s_block_count;
static char s_cart_path[256];
static bool s_active;
static bool s_owner_claimed;
static MDMA_HandleTypeDef s_hmdma_cache;
static bool s_mdma_ready;
static uint8_t s_bounce[LUA_CART_RES_CACHE_BOUNCE_SIZE] LUA_CART_RES_RAM_RUNTIME;

static uint32_t align_up_u32(uint32_t value, uint32_t align)
{
  uint32_t mask = align - 1u;
  return (value + mask) & ~mask;
}

static void clean_dcache_range(const void* ptr, uint32_t size)
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

static void invalidate_dcache_range(const void* ptr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (!ptr || size == 0u) return;
  uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)ptr + size + 31u) & ~(uintptr_t)31u;
  SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
  (void)ptr;
  (void)size;
#endif
}

static void make_sd_path(char* out, size_t out_size, const char* path)
{
  if (!out || out_size == 0u) return;
  if (!path) {
    out[0] = '\0';
  } else if (strchr(path, ':')) {
    snprintf(out, out_size, "%s", path);
  } else if (path[0] == '/') {
    snprintf(out, out_size, "%s%s", LUA_CART_RES_CACHE_SD_DRIVE, path);
  } else {
    snprintf(out, out_size, "%s/%s", LUA_CART_RES_CACHE_SD_DRIVE, path);
  }
}

static bool image_format_supported(uint8_t format)
{
  return format == XHGC_IMG_BGRA8888 || format == XHGC_IMG_RGB565;
}

static void arena_init(void)
{
  memset(s_blocks, 0, sizeof(s_blocks));
  s_blocks[0].offset = 0u;
  s_blocks[0].size = RESOURCE_ARENA_SIZE;
  s_blocks[0].free = 1u;
  s_block_count = 1u;
}

static void arena_remove_block(uint32_t index)
{
  if (index >= s_block_count) return;
  for (uint32_t i = index; i + 1u < s_block_count; ++i) {
    s_blocks[i] = s_blocks[i + 1u];
  }
  s_block_count--;
}

static void arena_coalesce(void)
{
  uint32_t i = 0;
  while (i + 1u < s_block_count) {
    LuaCartArenaBlock* cur = &s_blocks[i];
    LuaCartArenaBlock* next = &s_blocks[i + 1u];
    if (cur->free && next->free && cur->offset + cur->size == next->offset) {
      cur->size += next->size;
      arena_remove_block(i + 1u);
      continue;
    }
    i++;
  }
}

static void* arena_alloc(uint32_t size)
{
  size = align_up_u32(size, LUA_CART_RES_ALIGN);
  if (size == 0u) return NULL;

  for (uint32_t i = 0; i < s_block_count; ++i) {
    LuaCartArenaBlock* block = &s_blocks[i];
    if (!block->free || block->size < size) continue;

    if (block->size > size) {
      if (s_block_count >= LUA_CART_RES_CACHE_MAX_BLOCKS) return NULL;
      for (uint32_t j = s_block_count; j > i + 1u; --j) {
        s_blocks[j] = s_blocks[j - 1u];
      }
      s_blocks[i + 1u].offset = block->offset + size;
      s_blocks[i + 1u].size = block->size - size;
      s_blocks[i + 1u].free = 1u;
      s_block_count++;
      block = &s_blocks[i];
      block->size = size;
    }

    block->free = 0u;
    return (void*)(RESOURCE_ARENA_BASE + (uintptr_t)block->offset);
  }

  return NULL;
}

static void arena_free(const void* ptr)
{
  if (!ptr) return;
  uintptr_t addr = (uintptr_t)ptr;
  if (addr < RESOURCE_ARENA_BASE || addr > RESOURCE_ARENA_END) return;
  uint32_t offset = (uint32_t)(addr - RESOURCE_ARENA_BASE);

  for (uint32_t i = 0; i < s_block_count; ++i) {
    if (s_blocks[i].offset == offset && !s_blocks[i].free) {
      s_blocks[i].free = 1u;
      arena_coalesce();
      return;
    }
  }
}

static bool mdma_init_once(void)
{
  if (s_mdma_ready) return true;

  __HAL_RCC_MDMA_CLK_ENABLE();
  memset(&s_hmdma_cache, 0, sizeof(s_hmdma_cache));
  s_hmdma_cache.Instance = MDMA_Channel1;
  s_hmdma_cache.Init.Request = MDMA_REQUEST_SW;
  s_hmdma_cache.Init.TransferTriggerMode = MDMA_FULL_TRANSFER;
  s_hmdma_cache.Init.Priority = MDMA_PRIORITY_HIGH;
  s_hmdma_cache.Init.Endianness = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  s_hmdma_cache.Init.SourceInc = MDMA_SRC_INC_BYTE;
  s_hmdma_cache.Init.DestinationInc = MDMA_DEST_INC_BYTE;
  s_hmdma_cache.Init.SourceDataSize = MDMA_SRC_DATASIZE_BYTE;
  s_hmdma_cache.Init.DestDataSize = MDMA_DEST_DATASIZE_BYTE;
  s_hmdma_cache.Init.DataAlignment = MDMA_DATAALIGN_PACKENABLE;
  s_hmdma_cache.Init.BufferTransferLength = 128;
  s_hmdma_cache.Init.SourceBurst = MDMA_SOURCE_BURST_16BEATS;
  s_hmdma_cache.Init.DestBurst = MDMA_DEST_BURST_16BEATS;
  s_hmdma_cache.Init.SourceBlockAddressOffset = 0;
  s_hmdma_cache.Init.DestBlockAddressOffset = 0;

  if (HAL_MDMA_Init(&s_hmdma_cache) != HAL_OK) return false;
  s_mdma_ready = true;
  return true;
}

static bool mdma_copy(void* dst, const void* src, uint32_t size)
{
  if (size == 0u) return true;
  if (!dst || !src) return false;
  if (!mdma_init_once()) return false;

  clean_dcache_range(src, size);
  if (HAL_MDMA_Start(&s_hmdma_cache,
                     (uint32_t)(uintptr_t)src,
                     (uint32_t)(uintptr_t)dst,
                     size,
                     1u) != HAL_OK) {
    return false;
  }
  if (HAL_MDMA_PollForTransfer(&s_hmdma_cache, HAL_MDMA_FULL_TRANSFER, 1000u) != HAL_OK) {
    (void)HAL_MDMA_Abort(&s_hmdma_cache);
    return false;
  }
  invalidate_dcache_range(dst, size);
  return true;
}

static int find_entry(const char* path)
{
  if (!path) return -1;
  for (uint32_t i = 0; i < s_entry_count; ++i) {
    if (s_entries[i].listed && strcmp(s_entries[i].path, path) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static bool list_visitor(const char* path, const XhgcIndexEntry* entry, void* ctx)
{
  (void)ctx;
  if (!path || !entry) return true;
  if (entry->type != XHGC_RES_IMAGE || !image_format_supported(entry->format)) return true;
  if (s_entry_count >= LUA_CART_RES_CACHE_MAX_ENTRIES) return false;

  LuaCartResourceEntry* dst = &s_entries[s_entry_count++];
  memset(dst, 0, sizeof(*dst));
  snprintf(dst->path, sizeof(dst->path), "%s", path);
  dst->entry = *entry;
  dst->listed = 1u;
  return true;
}

static bool read_resource_to_arena(const LuaCartResourceEntry* ent, void* dst)
{
  char fatfs_path[256];
  XHGC_CartFatFs cart_file;
  XHGC_CartSlot data_slot;
  XHGC_CartFile file;
  uint32_t copied = 0u;

  if (!ent || !dst || s_cart_path[0] == '\0') return false;

  make_sd_path(fatfs_path, sizeof(fatfs_path), s_cart_path);
  if (xhgc_cart_open_fatfs(&cart_file, fatfs_path) != XHGC_CART_OK) return false;

  if (xhgc_cart_get_slot(&cart_file.cart, XHGC_CART_SLOT_DATA, &data_slot) != XHGC_CART_OK ||
      (uint64_t)ent->entry.data_off + ent->entry.size > data_slot.size) {
    xhgc_cart_close_fatfs(&cart_file);
    return false;
  }

  file.image_offset = data_slot.offset + ent->entry.data_off;
  file.data_offset = ent->entry.data_off;
  file.data_size = ent->entry.size;
  file.crc32 = ent->entry.crc32;

  while (copied < ent->entry.size) {
    uint32_t want = ent->entry.size - copied;
    if (want > sizeof(s_bounce)) want = sizeof(s_bounce);

    if (xhgc_cart_read_file(&cart_file.cart, &file, copied, s_bounce, want) != XHGC_CART_OK ||
        !mdma_copy((uint8_t*)dst + copied, s_bounce, want)) {
      xhgc_cart_close_fatfs(&cart_file);
      return false;
    }
    copied += want;
  }

  xhgc_cart_close_fatfs(&cart_file);
  return true;
}

static bool evict_one_unreferenced(const LuaCartResourceEntry* keep)
{
  for (uint32_t i = 0; i < s_entry_count; ++i) {
    LuaCartResourceEntry* ent = &s_entries[i];
    if (ent == keep || !ent->loaded || ent->refs != 0u) continue;
    arena_free((const void*)(RESOURCE_ARENA_BASE + (uintptr_t)ent->offset));
    ent->loaded = 0u;
    ent->offset = 0u;
    ent->size = 0u;
    return true;
  }

  return false;
}

static bool load_entry(LuaCartResourceEntry* ent, bool allow_evict)
{
  void* dst;

  if (!ent) return false;
  if (resource_arena_get_owner() != RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE) return false;
  if (ent->loaded) return true;
  dst = arena_alloc(ent->entry.size);
  while (!dst && allow_evict && evict_one_unreferenced(ent)) {
    dst = arena_alloc(ent->entry.size);
  }
  if (!dst) return false;
  if (!read_resource_to_arena(ent, dst)) {
    arena_free(dst);
    return false;
  }

  ent->offset = (uint32_t)((uintptr_t)dst - RESOURCE_ARENA_BASE);
  ent->size = ent->entry.size;
  ent->loaded = 1u;
  return true;
}

void lua_cart_resource_cache_reset(void)
{
  if (s_owner_claimed) {
    (void)resource_arena_release(RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE);
    s_owner_claimed = false;
  }
  memset(s_entries, 0, sizeof(s_entries));
  s_entry_count = 0u;
  s_cart_path[0] = '\0';
  s_active = false;
  arena_init();
}

int lua_cart_resource_cache_preload(const char* cart_path)
{
  char fatfs_path[256];
  XHGC_CartFatFs cart_file;
  int rc;

  lua_cart_resource_cache_reset();
  if (!cart_path || cart_path[0] == '\0') return -1;
  if (!resource_arena_claim(RESOURCE_ARENA_OWNER_CART_RESOURCE_CACHE)) {
    printf("lua_cart_resource_cache disabled: resource_manager owns RESOURCE_ARENA\r\n");
    return -2;
  }
  s_owner_claimed = true;
  snprintf(s_cart_path, sizeof(s_cart_path), "%s", cart_path);

  (void)SD_FATFS_Mount();
  make_sd_path(fatfs_path, sizeof(fatfs_path), cart_path);
  rc = xhgc_cart_open_fatfs(&cart_file, fatfs_path);
  if (rc != XHGC_CART_OK) return rc;

  rc = xhgc_cart_for_each_resource(&cart_file.cart, list_visitor, NULL);
  xhgc_cart_close_fatfs(&cart_file);
  if (rc != XHGC_CART_OK) {
    lua_cart_resource_cache_reset();
    return rc;
  }

  s_active = true;
  for (uint32_t i = 0; i < s_entry_count; ++i) {
    if (!load_entry(&s_entries[i], false)) {
      break;
    }
  }

  return 0;
}

bool lua_cart_resource_cache_acquire_image(const char* path,
                                           LuaCartCachedResource* out,
                                           const char** out_err)
{
  int index;
  LuaCartResourceEntry* ent;

  if (out) memset(out, 0, sizeof(*out));
  if (out_err) *out_err = NULL;
  if (!s_active) {
    if (out_err) *out_err = "cart resource cache is not active";
    return false;
  }

  index = find_entry(path);
  if (index < 0) {
    if (out_err) *out_err = "cart resource not found";
    return false;
  }

  ent = &s_entries[index];
  if (!ent->loaded && !load_entry(ent, true)) {
    if (out_err) *out_err = "cart app arena is full";
    return false;
  }

  if (ent->refs == UINT16_MAX) {
    if (out_err) *out_err = "cart resource reference overflow";
    return false;
  }
  ent->refs++;

  if (out) {
    out->data = (const uint8_t*)(RESOURCE_ARENA_BASE + (uintptr_t)ent->offset);
    out->size = ent->size;
    out->entry = ent->entry;
  }
  return true;
}

void lua_cart_resource_cache_release_image(const uint8_t* data)
{
  if (!data) return;
  for (uint32_t i = 0; i < s_entry_count; ++i) {
    LuaCartResourceEntry* ent = &s_entries[i];
    const uint8_t* entry_data = ent->loaded ?
        (const uint8_t*)(RESOURCE_ARENA_BASE + (uintptr_t)ent->offset) : NULL;
    if (entry_data != data) continue;

    if (ent->refs > 0u) ent->refs--;
    if (ent->refs == 0u) {
      arena_free(entry_data);
      ent->loaded = 0u;
      ent->offset = 0u;
      ent->size = 0u;
    }
    return;
  }
}

#else

static const char *disabled_reason(void)
{
  return "lua_cart_resource_cache disabled: resource_manager owns RESOURCE_ARENA";
}

void lua_cart_resource_cache_reset(void)
{
  printf("%s\r\n", disabled_reason());
}

int lua_cart_resource_cache_preload(const char* cart_path)
{
  (void)cart_path;
  printf("%s\r\n", disabled_reason());
  return -1;
}

bool lua_cart_resource_cache_acquire_image(const char* path,
                                           LuaCartCachedResource* out,
                                           const char** out_err)
{
  (void)path;
  if (out) memset(out, 0, sizeof(*out));
  if (out_err) *out_err = disabled_reason();
  printf("%s\r\n", disabled_reason());
  return false;
}

void lua_cart_resource_cache_release_image(const uint8_t* data)
{
  (void)data;
}

#endif
