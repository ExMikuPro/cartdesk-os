#include "app_arena.h"

#include <stdint.h>

#include "xhgc_meminfo.h"

#if XHGC_MEMINFO_SELFTEST_ENABLE
#include <stdio.h>

#include "resource_arena_owner.h"
#include "sdram_layout.h"
#endif

static int align_is_valid(size_t align)
{
  return align != 0u && (align & (align - 1u)) == 0u;
}

/**
 * @brief  判断arena是否需要纳入APP_ARENA_REST meminfo统计
 * @param  a: arena实例指针
 * @retval 非0=需要统计, 0=参数非法或arena不在APP_ARENA_REST
 * @note   - 本函数只按base地址查找zone，不检查整个arena范围
 *         - 统计标签当前固定归入RESOURCE，调用方需保持分配口径一致
 */
static int app_arena_meminfo_tracked(const app_arena_t *a)
{
  const XHGC_MemZoneDesc *zone;

  if (!a || !a->base) return 0;

  zone = xhgc_mem_find_zone_by_addr((uintptr_t)a->base);
  return zone && zone->id == XHGC_MEM_ZONE_APP_ARENA_REST;
}

static uint32_t app_arena_meminfo_size(size_t size)
{
  return size > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}

/**
 * @brief  记录一次app_arena成功分配的meminfo占用
 * @param  a: arena实例指针
 * @param  size: 本次分配实际消耗字节数，包含对齐填充
 * @retval None
 * @note   - 本函数只更新统计，不执行真实分配
 *         - 仅APP_ARENA_REST上的arena会记录RESOURCE标签占用
 *         - size为0时直接返回，超过32位时按UINT32_MAX统计
 */
static void app_arena_meminfo_alloc(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a) || size == 0u) return;

  /* TODO: split APP/TEMP/RESOURCE tags once app_arena carries allocation purpose. */
  (void)xhgc_meminfo_alloc_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                  app_arena_meminfo_size(size),
                                  XHGC_MEM_TAG_RESOURCE);
}

/**
 * @brief  记录一次app_arena回滚释放的meminfo占用
 * @param  a: arena实例指针
 * @param  size: 本次回滚释放的字节数
 * @retval None
 * @note   - 本函数只更新统计，不清零或释放底层内存
 *         - 仅APP_ARENA_REST上的arena会释放RESOURCE标签统计
 *         - 调用方必须保证size来自同一arena的used差值，避免统计口径不一致
 */
static void app_arena_meminfo_free(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a) || size == 0u) return;

  (void)xhgc_meminfo_free_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                 app_arena_meminfo_size(size),
                                 XHGC_MEM_TAG_RESOURCE);
}

/**
 * @brief  记录一次app_arena分配失败
 * @param  a: arena实例指针
 * @param  size: 失败的申请字节数
 * @retval None
 * @note   - 本函数只更新fail_count，不修改arena指针
 *         - 仅APP_ARENA_REST上的arena会记录RESOURCE标签失败统计
 */
static void app_arena_meminfo_fail(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a)) return;

  (void)xhgc_meminfo_fail_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                 app_arena_meminfo_size(size),
                                 XHGC_MEM_TAG_RESOURCE);
}

/**
 * @brief  初始化线性arena的地址范围
 * @param  a: arena实例指针
 * @param  base: arena起始地址
 * @param  size: arena可用字节数
 * @retval None
 * @note   - 本函数只设置base/ptr/end，不清零底层内存
 *         - 若base+size发生回绕，arena会被置为空
 */
void app_arena_init(app_arena_t *a, void *base, size_t size)
{
  if (!a) return;
  a->base = (uint8_t*)base;
  a->ptr = (uint8_t*)base;
  a->end = base ? (uint8_t*)base + size : NULL;
  if (base && a->end < a->base) {
    a->base = NULL;
    a->ptr = NULL;
    a->end = NULL;
  }
}

/**
 * @brief  从线性arena分配一块对齐内存
 * @param  a: arena实例指针
 * @param  size: 申请字节数
 * @param  align: 对齐字节数，必须为2的幂
 * @return 非NULL=分配成功返回对齐后的指针, NULL=参数非法或空间不足
 * @note   - 本函数只推进arena指针，不支持单块释放
 *         - 若arena位于APP_ARENA_REST，会按实际消耗字节数记录meminfo统计
 */
void *app_arena_alloc(app_arena_t *a, size_t size, size_t align)
{
  uintptr_t ptr;
  uintptr_t aligned;
  uintptr_t end;
  uintptr_t mask;
  size_t used_before;
  size_t used_after;

  if (!a || !a->base || !a->ptr || !a->end || size == 0u) {
    app_arena_meminfo_fail(a, size);
    return NULL;
  }
  if (!align_is_valid(align)) {
    app_arena_meminfo_fail(a, size);
    return NULL;
  }

  ptr = (uintptr_t)a->ptr;
  end = (uintptr_t)a->end;
  mask = (uintptr_t)align - 1u;
  if (UINTPTR_MAX - ptr < mask) {
    app_arena_meminfo_fail(a, size);
    return NULL;
  }
  aligned = (ptr + mask) & ~mask;
  if (aligned > end) {
    app_arena_meminfo_fail(a, size);
    return NULL;
  }
  if ((uintptr_t)size > end - aligned) {
    app_arena_meminfo_fail(a, size);
    return NULL;
  }

  used_before = app_arena_used(a);
  a->ptr = (uint8_t*)(aligned + size);
  used_after = app_arena_used(a);
  if (used_after > used_before) {
    app_arena_meminfo_alloc(a, used_after - used_before);
  }

  return (void*)aligned;
}

/**
 * @brief  获取当前arena回滚标记
 * @param  a: arena实例指针
 * @return 非NULL=当前写入位置, NULL=参数非法
 */
app_arena_mark_t app_arena_mark(app_arena_t *a)
{
  return a ? a->ptr : NULL;
}

/**
 * @brief  将arena回滚到指定标记位置
 * @param  a: arena实例指针
 * @param  mark: 由app_arena_mark返回的标记位置
 * @retval None
 * @note   - mark必须位于当前arena的base到end范围内
 *         - 若arena位于APP_ARENA_REST，会释放回滚区间对应的meminfo统计
 */
void app_arena_reset_to(app_arena_t *a, app_arena_mark_t mark)
{
  size_t used_before;
  size_t used_after;

  if (!a || !a->base || !a->end || !mark) return;
  if (mark < a->base || mark > a->end) return;
  used_before = app_arena_used(a);
  a->ptr = mark;
  used_after = app_arena_used(a);
  if (used_before > used_after) {
    app_arena_meminfo_free(a, used_before - used_after);
  }
}

/**
 * @brief  将arena重置到起始位置
 * @param  a: arena实例指针
 * @retval None
 * @note   - 本函数不清零底层内存
 *         - 若arena位于APP_ARENA_REST，会重置RESOURCE标签的meminfo used统计
 */
void app_arena_reset(app_arena_t *a)
{
  if (!a) return;
  a->ptr = a->base;
  if (app_arena_meminfo_tracked(a)) {
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_APP_ARENA_REST,
                                      XHGC_MEM_TAG_RESOURCE);
  }
}

/**
 * @brief  查询arena已使用字节数
 * @param  a: arena实例指针
 * @return 当前ptr相对base的偏移字节数，参数非法时返回0
 */
size_t app_arena_used(const app_arena_t *a)
{
  if (!a || !a->base || !a->ptr || a->ptr < a->base) return 0u;
  return (size_t)(a->ptr - a->base);
}

#if XHGC_MEMINFO_SELFTEST_ENABLE
static void app_arena_meminfo_selftest_dump(const char *stage)
{
  printf("[XHGC MEMINFO SELFTEST] %s\r\n", stage);
  xhgc_meminfo_dump();
}

/**
 * @brief  执行app_arena与meminfo联动自检
 * @retval true=自检通过, false=自检失败
 * @note   - 本函数会初始化RESOURCE_ARENA上的临时arena并执行一次分配/重置
 *         - 本函数会打印meminfo快照和RESOURCE_ARENA owner自检日志
 */
bool app_arena_meminfo_selftest(void)
{
  app_arena_t arena;
  XHGC_MemInfoSnapshot baseline;
  XHGC_MemInfoSnapshot after_alloc;
  XHGC_MemInfoSnapshot after_reset;
  void *ptr;
  bool pass;
  const uint32_t alloc_size = 4096u;

  if (!resource_arena_owner_selftest()) {
    return false;
  }

  app_arena_meminfo_selftest_dump("baseline");
  xhgc_meminfo_get_snapshot(&baseline);

  app_arena_init(&arena, (void*)RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE);
  ptr = app_arena_alloc(&arena, alloc_size, 32u);

  app_arena_meminfo_selftest_dump("after_alloc");
  xhgc_meminfo_get_snapshot(&after_alloc);

  app_arena_reset(&arena);

  app_arena_meminfo_selftest_dump("after_reset");
  xhgc_meminfo_get_snapshot(&after_reset);

  pass = ptr != NULL &&
         baseline.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used == 0u &&
         after_alloc.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used >= alloc_size &&
         after_alloc.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].peak >=
             after_alloc.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used &&
         after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used == 0u &&
         after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].peak >=
             after_alloc.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].peak &&
         after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].fail_count ==
             baseline.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].fail_count;

  printf("[XHGC MEMINFO SELFTEST] %s used_before=0x%08lX used_after=0x%08lX used_reset=0x%08lX peak=0x%08lX fail=%lu\r\n",
         pass ? "PASS" : "FAIL",
         (unsigned long)baseline.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used,
         (unsigned long)after_alloc.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used,
         (unsigned long)after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].used,
         (unsigned long)after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].peak,
         (unsigned long)after_reset.zone_stats[XHGC_MEM_ZONE_APP_ARENA_REST].fail_count);

  return pass;
}
#endif

/**
 * @brief  查询arena剩余字节数
 * @param  a: arena实例指针
 * @return 当前ptr到end之间的剩余字节数，参数非法时返回0
 */
size_t app_arena_remaining(const app_arena_t *a)
{
  if (!a || !a->ptr || !a->end || a->end < a->ptr) return 0u;
  return (size_t)(a->end - a->ptr);
}
