#include "app_arena.h"

#include <stdint.h>

static int align_is_valid(size_t align)
{
  return align != 0u && (align & (align - 1u)) == 0u;
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

  if (!a || !a->base || !a->ptr || !a->end || size == 0u) return NULL;
  if (!align_is_valid(align)) return NULL;

  ptr = (uintptr_t)a->ptr;
  end = (uintptr_t)a->end;
  mask = (uintptr_t)align - 1u;
  if (UINTPTR_MAX - ptr < mask) return NULL;
  aligned = (ptr + mask) & ~mask;
  if (aligned > end) return NULL;
  if ((uintptr_t)size > end - aligned) return NULL;

  a->ptr = (uint8_t*)(aligned + size);
  return (void*)aligned;
}

app_arena_mark_t app_arena_mark(app_arena_t *a)
{
  return a ? a->ptr : NULL;
}

void app_arena_reset_to(app_arena_t *a, app_arena_mark_t mark)
{
  if (!a || !a->base || !a->end || !mark) return;
  if (mark < a->base || mark > a->end) return;
  a->ptr = mark;
}

void app_arena_reset(app_arena_t *a)
{
  if (!a) return;
  a->ptr = a->base;
}

size_t app_arena_used(const app_arena_t *a)
{
  if (!a || !a->base || !a->ptr || a->ptr < a->base) return 0u;
  return (size_t)(a->ptr - a->base);
}

size_t app_arena_remaining(const app_arena_t *a)
{
  if (!a || !a->ptr || !a->end || a->end < a->ptr) return 0u;
  return (size_t)(a->end - a->ptr);
}
