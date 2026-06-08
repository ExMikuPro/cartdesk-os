#ifndef XHGC_MEMINFO_H
#define XHGC_MEMINFO_H

#include <stdbool.h>
#include <stdint.h>

#include "xhgc_memory_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XHGC_MEM_TAG_UNKNOWN = 0,
    XHGC_MEM_TAG_FRAMEBUFFER,
    XHGC_MEM_TAG_LVGL,
    XHGC_MEM_TAG_DMA,
    XHGC_MEM_TAG_LAUNCHER,
    XHGC_MEM_TAG_APP,
    XHGC_MEM_TAG_LUA,
    XHGC_MEM_TAG_RESOURCE,
    XHGC_MEM_TAG_TEXTURE,
    XHGC_MEM_TAG_AUDIO,
    XHGC_MEM_TAG_CART,
    XHGC_MEM_TAG_TEMP,
    XHGC_MEM_TAG_COLD,
    XHGC_MEM_TAG_NEWLIB,
    XHGC_MEM_TAG_FREERTOS,
    XHGC_MEM_TAG_COUNT
} XHGC_MemTag;

typedef struct {
    uint32_t total;
    uint32_t used;
    uint32_t peak;
    uint32_t reserved;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
} XHGC_MemZoneStats;

typedef struct {
    uint32_t used;
    uint32_t peak;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
} XHGC_MemTagStats;

typedef struct {
    XHGC_MemZoneStats zone_stats[XHGC_MEM_ZONE_COUNT];
    XHGC_MemTagStats tag_stats[XHGC_MEM_TAG_COUNT];
    uint32_t total_sdram;
    uint32_t total_used;
    uint32_t total_peak;
    uint32_t total_fail_count;
} XHGC_MemInfoSnapshot;

void xhgc_meminfo_init(void);
bool xhgc_meminfo_reserve(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);
bool xhgc_meminfo_release(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);
bool xhgc_meminfo_alloc_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);
bool xhgc_meminfo_free_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);
bool xhgc_meminfo_fail_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);
bool xhgc_meminfo_zone_reset_tag(XHGC_MemZoneId zone, XHGC_MemTag tag);
void xhgc_meminfo_get_snapshot(XHGC_MemInfoSnapshot *out);
void xhgc_meminfo_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_MEMINFO_H */
