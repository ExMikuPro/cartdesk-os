#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_State lua_State;

typedef struct {
    size_t used;
    size_t peak;
    size_t capacity;
    size_t alloc_fail_count;
} LuaVmMemoryStats;

typedef struct {
    uint8_t *base;
    size_t capacity;
    LuaVmMemoryStats stats;
    void *first_block;
    bool initialized;
} LuaVmAllocator;

int lua_vm_memory_init(void);
LuaVmAllocator *lua_vm_memory_allocator(void);
lua_State *lua_vm_newstate(void);
void *lua_vm_alloc(void *ud, void *ptr, size_t old_size, size_t new_size);
void lua_vm_memory_get_stats(LuaVmMemoryStats *out_stats);
uint32_t lua_vm_heap_used(void);
uint32_t lua_vm_heap_peak(void);
uint32_t lua_vm_heap_capacity(void);
uint32_t lua_vm_alloc_fail_count(void);
size_t lua_vm_heap_capacity_bytes(void);
void lua_vm_memory_print_stats(void);

#ifdef __cplusplus
}
#endif
