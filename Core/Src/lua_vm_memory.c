#include "lua_vm_memory.h"

#include <stdio.h>
#include <string.h>

#include "sdram.h"

#define LUA_VM_ALLOC_ALIGN 32u
#define LUA_VM_BLOCK_FREE  1u
#define LUA_VM_BLOCK_USED  0u
#define LUA_VM_BLOCK_MAGIC 0x4C55414Du

_Static_assert((LUA_HEAP_BASE % LUA_VM_ALLOC_ALIGN) == 0u,
               "Lua heap base must satisfy allocator alignment");
_Static_assert(LUA_HEAP_SIZE > sizeof(void *),
               "Lua heap must be large enough for allocator metadata");

typedef struct lua_vm_block {
    size_t size;
    struct lua_vm_block *prev;
    struct lua_vm_block *next;
    uint32_t free;
    uint32_t magic;
    uint8_t reserved[12];
} __attribute__((aligned(LUA_VM_ALLOC_ALIGN))) lua_vm_block_t;

_Static_assert((sizeof(lua_vm_block_t) % LUA_VM_ALLOC_ALIGN) == 0u,
               "Lua VM block header must preserve allocator alignment");

static LuaVmAllocator g_lua_allocator;

static void lua_vm_merge_next(lua_vm_block_t *block);

static size_t lua_vm_align_up(size_t size)
{
    const size_t mask = LUA_VM_ALLOC_ALIGN - 1u;
    if (size > SIZE_MAX - mask) {
        return 0u;
    }
    return (size + mask) & ~mask;
}

static uint8_t *lua_vm_block_payload(lua_vm_block_t *block)
{
    return (uint8_t *)block + sizeof(*block);
}

static bool lua_vm_block_valid(const LuaVmAllocator *allocator,
                               const lua_vm_block_t *block)
{
    const uintptr_t address = (uintptr_t)block;
    const uintptr_t base = (uintptr_t)allocator->base;
    const uintptr_t end = base + allocator->capacity;

    return allocator->initialized &&
           address >= base &&
           address <= end - sizeof(*block) &&
           block->magic == LUA_VM_BLOCK_MAGIC;
}

static void lua_vm_split_block(lua_vm_block_t *block, size_t size)
{
    const size_t remainder = block->size - size;
    if (remainder < sizeof(lua_vm_block_t) + LUA_VM_ALLOC_ALIGN) {
        return;
    }

    lua_vm_block_t *tail =
        (lua_vm_block_t *)(lua_vm_block_payload(block) + size);
    tail->size = remainder - sizeof(*tail);
    tail->prev = block;
    tail->next = block->next;
    tail->free = LUA_VM_BLOCK_FREE;
    tail->magic = LUA_VM_BLOCK_MAGIC;
    memset(tail->reserved, 0, sizeof(tail->reserved));

    if (tail->next != NULL) {
        tail->next->prev = tail;
    }
    block->next = tail;
    block->size = size;
    lua_vm_merge_next(tail);
}

static void lua_vm_merge_next(lua_vm_block_t *block)
{
    lua_vm_block_t *next = block->next;
    if (next == NULL || next->free != LUA_VM_BLOCK_FREE) {
        return;
    }

    block->size += sizeof(*next) + next->size;
    block->next = next->next;
    if (block->next != NULL) {
        block->next->prev = block;
    }
}

static void *lua_vm_allocate(LuaVmAllocator *allocator, size_t size)
{
    const size_t aligned_size = lua_vm_align_up(size);
    if (aligned_size == 0u) {
        ++allocator->stats.alloc_fail_count;
        return NULL;
    }

    lua_vm_block_t *block = allocator->first_block;
    while (block != NULL) {
        if (block->free == LUA_VM_BLOCK_FREE && block->size >= aligned_size) {
            lua_vm_split_block(block, aligned_size);
            block->free = LUA_VM_BLOCK_USED;
            allocator->stats.used += block->size;
            if (allocator->stats.used > allocator->stats.peak) {
                allocator->stats.peak = allocator->stats.used;
            }
            return lua_vm_block_payload(block);
        }
        block = block->next;
    }

    ++allocator->stats.alloc_fail_count;
    return NULL;
}

static void lua_vm_release(LuaVmAllocator *allocator, void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    lua_vm_block_t *block =
        (lua_vm_block_t *)((uint8_t *)ptr - sizeof(lua_vm_block_t));
    if (!lua_vm_block_valid(allocator, block) ||
        block->free == LUA_VM_BLOCK_FREE) {
        return;
    }

    allocator->stats.used -= block->size;
    block->free = LUA_VM_BLOCK_FREE;
    lua_vm_merge_next(block);
    if (block->prev != NULL && block->prev->free == LUA_VM_BLOCK_FREE) {
        block = block->prev;
        lua_vm_merge_next(block);
    }
}

int lua_vm_memory_init(void)
{
    if (g_lua_allocator.base == NULL) {
        g_lua_allocator.base = (uint8_t *)LUA_HEAP_BASE;
        g_lua_allocator.capacity = LUA_HEAP_SIZE;
    }

    memset(g_lua_allocator.base, 0, sizeof(lua_vm_block_t));
    lua_vm_block_t *first = (lua_vm_block_t *)g_lua_allocator.base;
    first->size = g_lua_allocator.capacity - sizeof(*first);
    first->free = LUA_VM_BLOCK_FREE;
    first->magic = LUA_VM_BLOCK_MAGIC;

    g_lua_allocator.first_block = first;
    g_lua_allocator.stats.used = 0u;
    g_lua_allocator.stats.peak = 0u;
    g_lua_allocator.stats.capacity = g_lua_allocator.capacity;
    g_lua_allocator.stats.alloc_fail_count = 0u;
    g_lua_allocator.initialized = true;
    return 0;
}

LuaVmAllocator *lua_vm_memory_allocator(void)
{
    return &g_lua_allocator;
}

void *lua_vm_alloc(void *ud, void *ptr, size_t old_size, size_t new_size)
{
    LuaVmAllocator *allocator = ud;
    (void)old_size;

    if (allocator == NULL || !allocator->initialized) {
        return NULL;
    }
    if (new_size == 0u) {
        lua_vm_release(allocator, ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return lua_vm_allocate(allocator, new_size);
    }

    lua_vm_block_t *block =
        (lua_vm_block_t *)((uint8_t *)ptr - sizeof(lua_vm_block_t));
    if (!lua_vm_block_valid(allocator, block) ||
        block->free == LUA_VM_BLOCK_FREE) {
        ++allocator->stats.alloc_fail_count;
        return NULL;
    }

    const size_t aligned_size = lua_vm_align_up(new_size);
    if (aligned_size == 0u) {
        ++allocator->stats.alloc_fail_count;
        return NULL;
    }

    if (aligned_size <= block->size) {
        const size_t old_block_size = block->size;
        lua_vm_split_block(block, aligned_size);
        allocator->stats.used -= old_block_size - block->size;
        return ptr;
    }

    const size_t old_block_size = block->size;
    if (block->next != NULL &&
        block->next->free == LUA_VM_BLOCK_FREE &&
        old_block_size + sizeof(lua_vm_block_t) + block->next->size >= aligned_size) {
        lua_vm_merge_next(block);
        lua_vm_split_block(block, aligned_size);
        allocator->stats.used += block->size - old_block_size;
        if (allocator->stats.used > allocator->stats.peak) {
            allocator->stats.peak = allocator->stats.used;
        }
        return ptr;
    }

    void *new_ptr = lua_vm_allocate(allocator, new_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    memcpy(new_ptr, ptr, old_block_size < new_size ? old_block_size : new_size);
    lua_vm_release(allocator, ptr);
    return new_ptr;
}

void lua_vm_memory_get_stats(LuaVmMemoryStats *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = g_lua_allocator.stats;
    }
}

void lua_vm_memory_print_stats(void)
{
    printf("Lua heap region: SDRAM_APP_ARENA\r\n");
    printf("Lua heap base: 0x%08lX\r\n",
           (unsigned long)(uintptr_t)g_lua_allocator.base);
    printf("Lua heap size: %lu bytes\r\n",
           (unsigned long)g_lua_allocator.stats.capacity);
    printf("Lua heap used: %lu bytes\r\n",
           (unsigned long)g_lua_allocator.stats.used);
    printf("Lua heap peak: %lu bytes\r\n",
           (unsigned long)g_lua_allocator.stats.peak);
    printf("Lua heap alloc fail count: %lu\r\n",
           (unsigned long)g_lua_allocator.stats.alloc_fail_count);
}
