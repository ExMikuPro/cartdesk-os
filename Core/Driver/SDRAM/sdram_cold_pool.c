#include "sdram_cold_pool.h"

#include <stdint.h>
#include <string.h>

#include "sdram_layout.h"

static size_t s_cold_pool_offset = 0u;
static int s_cold_pool_initialized = 0;

static int cold_pool_align_valid(size_t align)
{
    return (align != 0u) && ((align & (align - 1u)) == 0u);
}

void cold_pool_init(void)
{
    s_cold_pool_offset = 0u;
    s_cold_pool_initialized = 1;
}

void cold_pool_reset(void)
{
    if (!s_cold_pool_initialized) {
        return;
    }

    s_cold_pool_offset = 0u;
}

void *cold_alloc(size_t size, size_t align)
{
    uintptr_t current;
    uintptr_t aligned;
    size_t new_offset;

    if (!s_cold_pool_initialized || size == 0u) {
        return NULL;
    }

    if (align < SDRAM_DEFAULT_ALIGN) {
        align = SDRAM_DEFAULT_ALIGN;
    }

    if (!cold_pool_align_valid(align)) {
        return NULL;
    }

    current = COLD_POOL_BASE + s_cold_pool_offset;
    aligned = sdram_align_up_uintptr(current, align);
    new_offset = (size_t)(aligned - COLD_POOL_BASE);

    if (new_offset > COLD_POOL_SIZE || size > ((size_t)COLD_POOL_SIZE - new_offset)) {
        return NULL;
    }

    s_cold_pool_offset = new_offset + size;
    return (void *)aligned;
}

void *cold_calloc(size_t count, size_t size, size_t align)
{
    void *ptr;
    size_t total;

    if (count != 0u && size > (SIZE_MAX / count)) {
        return NULL;
    }

    total = count * size;
    ptr = cold_alloc(total, align);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }

    return ptr;
}

size_t cold_pool_used(void)
{
    return s_cold_pool_offset;
}

size_t cold_pool_free(void)
{
    return (size_t)COLD_POOL_SIZE - s_cold_pool_offset;
}
