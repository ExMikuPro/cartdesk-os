#include "app_arena.h"

#include <stdint.h>

#include "xhgc_meminfo.h"

#if XHGC_MEMINFO_SELFTEST_ENABLE
#include <stdio.h>

#include "sdram_layout.h"
#endif

static int align_is_valid(size_t align)
{
  return align != 0u && (align & (align - 1u)) == 0u;
}

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

static void app_arena_meminfo_alloc(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a) || size == 0u) return;

  /* TODO: split APP/TEMP/RESOURCE tags once app_arena carries allocation purpose. */
  (void)xhgc_meminfo_alloc_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                  app_arena_meminfo_size(size),
                                  XHGC_MEM_TAG_RESOURCE);
}

static void app_arena_meminfo_free(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a) || size == 0u) return;

  (void)xhgc_meminfo_free_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                 app_arena_meminfo_size(size),
                                 XHGC_MEM_TAG_RESOURCE);
}

static void app_arena_meminfo_fail(const app_arena_t *a, size_t size)
{
  if (!app_arena_meminfo_tracked(a)) return;

  (void)xhgc_meminfo_fail_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                 app_arena_meminfo_size(size),
                                 XHGC_MEM_TAG_RESOURCE);
}

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

app_arena_mark_t app_arena_mark(app_arena_t *a)
{
  return a ? a->ptr : NULL;
}

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

void app_arena_reset(app_arena_t *a)
{
  if (!a) return;
  a->ptr = a->base;
  if (app_arena_meminfo_tracked(a)) {
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_APP_ARENA_REST,
                                      XHGC_MEM_TAG_RESOURCE);
  }
}

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

bool app_arena_meminfo_selftest(void)
{
  app_arena_t arena;
  XHGC_MemInfoSnapshot baseline;
  XHGC_MemInfoSnapshot after_alloc;
  XHGC_MemInfoSnapshot after_reset;
  void *ptr;
  bool pass;
  const uint32_t alloc_size = 4096u;

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

size_t app_arena_remaining(const app_arena_t *a)
{
  if (!a || !a->ptr || !a->end || a->end < a->ptr) return 0u;
  return (size_t)(a->end - a->ptr);
}
