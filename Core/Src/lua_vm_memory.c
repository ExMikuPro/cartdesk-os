#include "lua_vm_memory.h"

#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "sdram.h"
#include "xhgc_meminfo.h"

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

static uint32_t lua_vm_meminfo_size(size_t size)
{
    return size > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}

static void lua_vm_meminfo_alloc(size_t size)
{
    if (size == 0u) {
        return;
    }

    (void)xhgc_meminfo_alloc_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                    lua_vm_meminfo_size(size),
                                    XHGC_MEM_TAG_LUA);
}

static void lua_vm_meminfo_free(size_t size)
{
    if (size == 0u) {
        return;
    }

    (void)xhgc_meminfo_free_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                   lua_vm_meminfo_size(size),
                                   XHGC_MEM_TAG_LUA);
}

static void lua_vm_meminfo_fail(size_t size)
{
    (void)xhgc_meminfo_fail_record(XHGC_MEM_ZONE_APP_ARENA_REST,
                                   lua_vm_meminfo_size(size),
                                   XHGC_MEM_TAG_LUA);
}

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

static size_t lua_vm_configured_capacity(void)
{
    return (size_t)LUA_HEAP_SIZE;
}

/**
 * @brief  将空闲/可用块按目标payload大小拆分出尾部空闲块
 * @param  block: 待拆分的块头
 * @param  size: 目标payload字节数，已按Lua堆对齐
 * @retval None
 * @note   - 剩余空间不足以容纳块头和最小对齐payload时不会拆分
 *         - 拆分会重建双向链表，并尝试合并新尾块之后的相邻空闲块
 *         - 本函数不更新allocator used/peak或meminfo统计
 */
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

/**
 * @brief  将当前块与其后继空闲块合并
 * @param  block: 当前块头
 * @retval None
 * @note   - 仅当next存在且标记为空闲时合并
 *         - 合并会更新双向链表指针，不清零被吞并块头
 *         - 本函数不修改allocator used/peak或meminfo统计
 */
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

/**
 * @brief  在Lua VM堆链表中查找并分配一块payload
 * @param  allocator: Lua VM allocator实例
 * @param  size: Lua请求的字节数
 * @return 非NULL=分配成功的payload指针, NULL=对齐溢出或无可用块
 * @note   - 请求会向上对齐到LUA_VM_ALLOC_ALIGN
 *         - 成功路径会拆分块、标记占用、更新used/peak和meminfo
 *         - 失败路径会增加alloc_fail_count并记录LUA标签fail_count
 */
static void *lua_vm_allocate(LuaVmAllocator *allocator, size_t size)
{
    const size_t aligned_size = lua_vm_align_up(size);
    if (aligned_size == 0u) {
        ++allocator->stats.alloc_fail_count;
        lua_vm_meminfo_fail(size);
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
            lua_vm_meminfo_alloc(block->size);
            return lua_vm_block_payload(block);
        }
        block = block->next;
    }

    ++allocator->stats.alloc_fail_count;
    lua_vm_meminfo_fail(size);
    return NULL;
}

/**
 * @brief  释放Lua VM堆payload并尝试合并相邻空闲块
 * @param  allocator: Lua VM allocator实例
 * @param  ptr: 待释放payload指针
 * @retval None
 * @note   - NULL、非法块或已释放块会被忽略
 *         - 成功释放会减少used并回退meminfo LUA占用
 *         - 会先合并后继空闲块，再在前驱为空闲时向前合并
 */
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
    lua_vm_meminfo_free(block->size);
    block->free = LUA_VM_BLOCK_FREE;
    lua_vm_merge_next(block);
    if (block->prev != NULL && block->prev->free == LUA_VM_BLOCK_FREE) {
        block = block->prev;
        lua_vm_merge_next(block);
    }
}

/**
 * @brief  初始化Lua VM专用堆分配器
 * @retval 0=初始化成功
 * @note   - 首次调用会绑定LUA_HEAP_BASE/LUA_HEAP_SIZE作为allocator区域
 *         - 会重建首个空闲块、清零Lua堆统计并标记allocator已初始化
 *         - 会重置APP_ARENA_REST中LUA标签的meminfo动态用量
 */
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
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_APP_ARENA_REST,
                                      XHGC_MEM_TAG_LUA);
    return 0;
}

/**
 * @brief  获取Lua VM全局allocator实例
 * @return 非NULL=LuaVmAllocator全局实例指针
 * @note   - 返回对象由本模块持有，调用方不得释放或替换
 */
LuaVmAllocator *lua_vm_memory_allocator(void)
{
    return &g_lua_allocator;
}

/**
 * @brief  使用Lua VM专用allocator创建Lua状态机
 * @return 非NULL=创建成功的lua_State指针, NULL=allocator初始化或lua_newstate失败
 * @note   - 会先调用lua_vm_memory_init重置Lua堆
 *         - Lua主分配路径为lua_vm_alloc
 */
lua_State *lua_vm_newstate(void)
{
    if (lua_vm_memory_init() != 0) {
        return NULL;
    }

    return lua_newstate(lua_vm_alloc, lua_vm_memory_allocator());
}

/**
 * @brief  Lua VM allocator回调
 * @param  ud: LuaVmAllocator实例指针
 * @param  ptr: 旧payload指针，NULL表示新分配
 * @param  old_size: Lua传入的旧大小，当前实现不使用
 * @param  new_size: 新大小，0表示释放
 * @return 非NULL=分配或重分配成功, NULL=释放完成或分配失败
 * @note   - 新分配、释放和重分配都会同步维护Lua堆统计
 *         - 成功/失败路径会同步更新meminfo中的LUA标签统计
 *         - 本函数只操作LUA_HEAP分区，不改变RESOURCE_ARENA owner
 */
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
        lua_vm_meminfo_fail(new_size);
        return NULL;
    }

    const size_t aligned_size = lua_vm_align_up(new_size);
    if (aligned_size == 0u) {
        ++allocator->stats.alloc_fail_count;
        lua_vm_meminfo_fail(new_size);
        return NULL;
    }

    if (aligned_size <= block->size) {
        const size_t old_block_size = block->size;
        lua_vm_split_block(block, aligned_size);
        const size_t released_size = old_block_size - block->size;
        allocator->stats.used -= released_size;
        lua_vm_meminfo_free(released_size);
        return ptr;
    }

    const size_t old_block_size = block->size;
    if (block->next != NULL &&
        block->next->free == LUA_VM_BLOCK_FREE &&
        old_block_size + sizeof(lua_vm_block_t) + block->next->size >= aligned_size) {
        lua_vm_merge_next(block);
        lua_vm_split_block(block, aligned_size);
        const size_t added_size = block->size - old_block_size;
        allocator->stats.used += added_size;
        if (allocator->stats.used > allocator->stats.peak) {
            allocator->stats.peak = allocator->stats.used;
        }
        lua_vm_meminfo_alloc(added_size);
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

/**
 * @brief  获取Lua VM堆统计快照
 * @param  out_stats: 输出统计结构体指针，NULL时不执行任何操作
 * @retval None
 */
void lua_vm_memory_get_stats(LuaVmMemoryStats *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = g_lua_allocator.stats;
        if (out_stats->capacity == 0u) {
            out_stats->capacity = lua_vm_configured_capacity();
        }
    }
}

static uint32_t lua_vm_stat_to_u32(size_t value)
{
    return value > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

uint32_t lua_vm_heap_used(void)
{
    return lua_vm_stat_to_u32(g_lua_allocator.stats.used);
}

uint32_t lua_vm_heap_peak(void)
{
    return lua_vm_stat_to_u32(g_lua_allocator.stats.peak);
}

uint32_t lua_vm_heap_capacity(void)
{
    size_t capacity = g_lua_allocator.stats.capacity;
    if (capacity == 0u) {
        capacity = lua_vm_configured_capacity();
    }
    return lua_vm_stat_to_u32(capacity);
}

uint32_t lua_vm_alloc_fail_count(void)
{
    return lua_vm_stat_to_u32(g_lua_allocator.stats.alloc_fail_count);
}

size_t lua_vm_heap_capacity_bytes(void)
{
    return lua_vm_configured_capacity();
}

/**
 * @brief  打印Lua VM堆统计信息
 * @retval None
 * @note   - 输出base、capacity、used、peak和alloc_fail_count
 *         - 本函数不修改allocator状态
 */
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
