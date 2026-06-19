#include "resource_manager.h"

#include <stddef.h>
#include <string.h>

#include "app_arena.h"
#include "main.h"
#include "resource_arena_owner.h"
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
static uint32_t s_scene_arena_peak_bytes;

static res_handle_t invalid_handle(void)
{
  res_handle_t h = { RES_HANDLE_INVALID_INDEX, 0u };
  return h;
}

/**
 * @brief  推进资源记录generation以失效旧handle
 * @param  rec: 资源记录指针，NULL时直接返回
 * @retval None
 * @note   - generation回绕到0时会跳到1，避免生成invalid handle的generation值
 *         - 本函数只修改单条资源记录，不释放资源内存
 */
static void bump_generation(res_record_t *rec)
{
  if (!rec) return;
  rec->generation++;
  if (rec->generation == 0u) rec->generation = 1u;
}

/**
 * @brief  清理资源像素缓冲对应的DCache范围
 * @param  ptr: 像素缓冲起始地址
 * @param  size: 需要清理的字节数
 * @retval None
 * @note   - 维护范围会向外扩展到32字节cache line边界
 *         - 用于cart数据读入RESOURCE_ARENA后交给LVGL/DMA读取
 *         - 平台未声明DCache时为空操作
 */
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

static uint32_t arena_size_to_u32(size_t size)
{
  return size > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}

static void res_manager_track_peak(void)
{
  uint32_t used = arena_size_to_u32(app_arena_used(&s_scene_arena));
  if (used > s_scene_arena_peak_bytes) {
    s_scene_arena_peak_bytes = used;
  }
}

/**
 * @brief  初始化资源管理器并申请RESOURCE_ARENA所有权
 * @retval None
 * @note   - 成功时会把RESOURCE_ARENA绑定为场景arena并清空资源记录表
 *         - 失败时会清空本地状态、重置cart index并记录last_error
 *         - 本函数会修改全局资源管理器状态和RESOURCE_ARENA owner
 */
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

  app_arena_init(&s_scene_arena, (void*)RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_initialized = true;
  s_last_error = NULL;
  s_scene_arena_peak_bytes = 0u;
  cart_index_reset();
}

/**
 * @brief  挂载cart资源索引并建立资源记录表
 * @param  cart_path: cart文件路径
 * @retval true=挂载并索引成功, false=初始化失败、索引加载失败或资源数量超限
 * @note   - 未初始化时会先调用res_manager_init
 *         - 会重置场景arena、清空旧记录并重新加载cart index
 *         - 本函数不读取资源像素数据，只记录cart资源元信息
 */
bool res_manager_mount_cart(const char *cart_path)
{
  if (!s_initialized) res_manager_init();
  if (!s_initialized) return false;
  app_arena_reset(&s_scene_arena);
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_scene_arena_peak_bytes = 0u;

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

/**
 * @brief  从当前cart资源索引获取或加载一张BGRA8888图片
 * @param  path: cart内资源路径
 * @param  life: 资源生命周期标记
 * @return 有效handle=获取成功, invalid handle=失败
 * @note   - 未初始化时会先调用res_manager_init
 *         - 首次加载会从RESOURCE_ARENA分配像素缓冲并读取cart数据
 *         - 读取成功后会清理像素缓冲对应DCache，供DMA/LVGL读取
 *         - 已加载资源会增加refcount并复用原像素缓冲
 */
res_handle_t res_acquire_image(const char *path, res_lifetime_t life)
{
  const cart_res_meta_t *meta;
  int index;
  res_record_t *rec;
  app_arena_mark_t mark;
  void *pixels;

  s_last_error = NULL;
  if (!s_initialized) res_manager_init();
  if (!s_initialized) return invalid_handle();
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
  res_manager_track_peak();
  return (res_handle_t){ (uint16_t)index, rec->generation };
}

/**
 * @brief  从资源场景arena分配图片视图临时缓冲
 * @param  size: 申请字节数
 * @param  align: 对齐字节数，按app_arena_alloc规则校验
 * @return 非NULL=分配成功返回缓冲指针, NULL=初始化失败或空间不足
 * @note   - 未初始化时会先调用res_manager_init
 *         - 缓冲归RESOURCE_ARENA场景arena统一管理，不支持单块释放
 */
void *res_alloc_image_view_buffer(size_t size, size_t align)
{
  void *pixels;

  s_last_error = NULL;
  if (!s_initialized) res_manager_init();
  if (!s_initialized) return NULL;

  pixels = app_arena_alloc(&s_scene_arena, size, align);
  if (!pixels) {
    s_last_error = "not enough app arena memory for image view";
  } else {
    res_manager_track_peak();
  }
  return pixels;
}

/**
 * @brief  校验资源句柄是否仍指向可用记录
 * @param  h: 待校验资源句柄
 * @retval true=句柄对应READY或READY_UNUSED记录, false=索引越界、generation不匹配或状态不可用
 */
bool res_handle_valid(res_handle_t h)
{
  if (h.index >= s_record_count) return false;
  if (s_records[h.index].generation != h.generation) return false;
  return s_records[h.index].state == RES_READY ||
         s_records[h.index].state == RES_READY_UNUSED;
}

/**
 * @brief  通过资源句柄获取图片资源描述
 * @param  h: 图片资源句柄
 * @return 非NULL=有效图片资源描述指针, NULL=句柄无效
 * @note   - 返回指针归resource_manager内部记录表所有，调用方不得释放
 */
const image_resource_t *res_get_image(res_handle_t h)
{
  if (!res_handle_valid(h)) return NULL;
  return &s_records[h.index].image;
}

/**
 * @brief  释放一次图片资源引用
 * @param  h: 图片资源句柄
 * @retval None
 * @note   - 句柄无效时直接返回
 *         - refcount递减到0后资源状态变为READY_UNUSED
 *         - 本函数不立即回收RESOURCE_ARENA像素内存
 */
void res_release(res_handle_t h)
{
  res_record_t *rec;
  if (!res_handle_valid(h)) return;
  rec = &s_records[h.index];
  if (rec->refcount > 0u) rec->refcount--;
  if (rec->refcount == 0u) rec->state = RES_READY_UNUSED;
}

/**
 * @brief  重置场景资源并释放RESOURCE_ARENA所有权
 * @retval None
 * @note   - 会reset场景arena、清空资源记录表和cart index
 *         - 会把resource_manager初始化状态置为false
 *         - 会调用resource_arena_release释放RESOURCE_ARENA owner
 */
void res_scene_reset(void)
{
  if (!s_initialized) return;
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
  memset(&s_scene_arena, 0, sizeof(s_scene_arena));
  memset(s_records, 0, sizeof(s_records));
  s_record_count = 0u;
  s_initialized = false;
  s_last_error = NULL;
  s_scene_arena_peak_bytes = 0u;
  cart_index_reset();
  (void)resource_arena_release(RESOURCE_ARENA_OWNER_RESOURCE_MANAGER);
}

/**
 * @brief  获取资源管理器最后一次错误描述
 * @return 非NULL=错误描述字符串, NULL=当前无错误
 */
const char *res_last_error(void)
{
  return s_last_error;
}

uint32_t res_manager_used_bytes(void)
{
  return arena_size_to_u32(app_arena_used(&s_scene_arena));
}

uint32_t res_manager_peak_bytes(void)
{
  return s_scene_arena_peak_bytes;
}

uint32_t res_manager_capacity_bytes(void)
{
  return (uint32_t)RESOURCE_ARENA_SIZE;
}

uint32_t res_manager_alive_count(void)
{
  uint32_t count = 0u;

  for (uint16_t i = 0; i < s_record_count; ++i) {
    if (s_records[i].state == RES_READY || s_records[i].state == RES_READY_UNUSED) {
      ++count;
    }
  }
  return count;
}

uint32_t res_manager_indexed_count(void)
{
  return (uint32_t)s_record_count;
}

uint32_t res_manager_refcount_anomaly_count(void)
{
  /* currently unavailable: resource manager does not classify refcount anomalies yet. */
  return 0u;
}
