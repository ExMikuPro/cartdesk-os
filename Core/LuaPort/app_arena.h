#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *base;
  uint8_t *ptr;
  uint8_t *end;
} app_arena_t;

typedef uint8_t *app_arena_mark_t;

void app_arena_init(app_arena_t *a, void *base, size_t size);
void *app_arena_alloc(app_arena_t *a, size_t size, size_t align);
app_arena_mark_t app_arena_mark(app_arena_t *a);
void app_arena_reset_to(app_arena_t *a, app_arena_mark_t mark);
void app_arena_reset(app_arena_t *a);
size_t app_arena_used(const app_arena_t *a);
size_t app_arena_remaining(const app_arena_t *a);

#ifdef __cplusplus
}
#endif
