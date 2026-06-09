#include "xhgc_meminfo.h"

#include <stdio.h>
#include <string.h>

#define XHGC_MEMINFO_FRAMEBUFFER_SIZE ((uint32_t)0x00177000UL)

static XHGC_MemZoneStats g_xhgc_meminfo_zone_stats[XHGC_MEM_ZONE_COUNT];
static XHGC_MemTagStats g_xhgc_meminfo_tag_stats[XHGC_MEM_TAG_COUNT];
static uint32_t g_xhgc_meminfo_total_sdram;
static uint32_t g_xhgc_meminfo_total_peak;
static bool g_xhgc_meminfo_initialized;

static const char *const g_xhgc_meminfo_tag_names[XHGC_MEM_TAG_COUNT] = {
    "UNKNOWN",
    "FRAMEBUFFER",
    "LVGL",
    "DMA",
    "LAUNCHER",
    "APP",
    "LUA",
    "RESOURCE",
    "TEXTURE",
    "AUDIO",
    "CART",
    "TEMP",
    "COLD",
    "NEWLIB",
    "FREERTOS"
};

static bool xhgc_meminfo_is_valid_zone(XHGC_MemZoneId zone)
{
    return (int)zone >= 0 && zone < XHGC_MEM_ZONE_COUNT;
}

static bool xhgc_meminfo_is_valid_tag(XHGC_MemTag tag)
{
    return (int)tag >= 0 && tag < XHGC_MEM_TAG_COUNT;
}

static bool xhgc_meminfo_is_fixed_framebuffer_zone(XHGC_MemZoneId zone)
{
    const XHGC_MemZoneDesc *desc;

    if (!xhgc_meminfo_is_valid_zone(zone)) {
        return false;
    }

    desc = xhgc_mem_get_zone(zone);
    return desc != NULL &&
           (desc->flags & XHGC_MEM_ZONE_FLAG_FIXED) != 0u &&
           (desc->flags & XHGC_MEM_ZONE_FLAG_FRAMEBUFFER) != 0u;
}

static uint32_t xhgc_meminfo_current_total_used(void)
{
    uint32_t total = 0u;

    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        total += g_xhgc_meminfo_zone_stats[i].used;
    }

    return total;
}

static uint32_t xhgc_meminfo_current_total_fail_count(void)
{
    uint32_t total = 0u;

    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        total += g_xhgc_meminfo_zone_stats[i].fail_count;
    }

    return total;
}

/**
 * @brief  刷新指定zone/tag以及全局used峰值
 * @param  zone: 已校验有效的内存zone
 * @param  tag: 已校验有效的内存用途标签
 * @retval None
 * @note   - 调用方必须先完成zone/tag合法性检查
 *         - 本函数只根据当前used更新peak，不修改used/reserved/fail计数
 *         - 全局峰值按所有zone当前used重新汇总后比较刷新
 */
static void xhgc_meminfo_update_peaks(XHGC_MemZoneId zone, XHGC_MemTag tag)
{
    XHGC_MemZoneStats *zone_stats = &g_xhgc_meminfo_zone_stats[zone];
    XHGC_MemTagStats *tag_stats = &g_xhgc_meminfo_tag_stats[tag];
    uint32_t total_used;

    if (zone_stats->used > zone_stats->peak) {
        zone_stats->peak = zone_stats->used;
    }

    if (tag_stats->used > tag_stats->peak) {
        tag_stats->peak = tag_stats->used;
    }

    total_used = xhgc_meminfo_current_total_used();
    if (total_used > g_xhgc_meminfo_total_peak) {
        g_xhgc_meminfo_total_peak = total_used;
    }
}

/**
 * @brief  按zone/tag增加当前占用并按需记录分配或保留计数
 * @param  zone: 目标内存zone
 * @param  size: 本次增加的字节数
 * @param  tag: 内存用途标签
 * @param  count_alloc: true=增加alloc_count
 * @param  count_reserved: true=同步增加reserved基线
 * @retval true=统计更新成功, false=未初始化、参数非法、size为0或容量不足
 * @note   - 本函数只更新meminfo统计，不执行真实内存分配
 *         - 更新成功后会刷新zone/tag和全局used峰值
 *         - 调用方必须保证size与真实分配或保留口径一致
 */
static bool xhgc_meminfo_add_usage(XHGC_MemZoneId zone,
                                   uint32_t size,
                                   XHGC_MemTag tag,
                                   bool count_alloc,
                                   bool count_reserved)
{
    XHGC_MemZoneStats *zone_stats;
    XHGC_MemTagStats *tag_stats;

    if (!g_xhgc_meminfo_initialized ||
        !xhgc_meminfo_is_valid_zone(zone) ||
        !xhgc_meminfo_is_valid_tag(tag) ||
        size == 0u) {
        return false;
    }

    zone_stats = &g_xhgc_meminfo_zone_stats[zone];
    tag_stats = &g_xhgc_meminfo_tag_stats[tag];

    if (zone_stats->used > zone_stats->total ||
        size > (zone_stats->total - zone_stats->used)) {
        return false;
    }

    if (count_reserved &&
        size > (zone_stats->total - zone_stats->reserved)) {
        return false;
    }

    zone_stats->used += size;
    tag_stats->used += size;

    if (count_reserved) {
        zone_stats->reserved += size;
    }

    if (count_alloc) {
        ++zone_stats->alloc_count;
        ++tag_stats->alloc_count;
    }

    xhgc_meminfo_update_peaks(zone, tag);
    return true;
}

/**
 * @brief  按zone/tag减少当前占用并按需记录释放或保留释放计数
 * @param  zone: 目标内存zone
 * @param  size: 本次减少的字节数
 * @param  tag: 内存用途标签
 * @param  count_free: true=增加free_count
 * @param  count_reserved: true=同步减少reserved基线
 * @retval true=统计更新成功, false=未初始化、参数非法、size为0或统计不足
 * @note   - 本函数只更新meminfo统计，不释放真实内存
 *         - used/tag used/reserved不足时会拒绝，避免统计下溢
 *         - peak和fail_count在释放路径上保持不变
 */
static bool xhgc_meminfo_sub_usage(XHGC_MemZoneId zone,
                                   uint32_t size,
                                   XHGC_MemTag tag,
                                   bool count_free,
                                   bool count_reserved)
{
    XHGC_MemZoneStats *zone_stats;
    XHGC_MemTagStats *tag_stats;

    if (!g_xhgc_meminfo_initialized ||
        !xhgc_meminfo_is_valid_zone(zone) ||
        !xhgc_meminfo_is_valid_tag(tag) ||
        size == 0u) {
        return false;
    }

    zone_stats = &g_xhgc_meminfo_zone_stats[zone];
    tag_stats = &g_xhgc_meminfo_tag_stats[tag];

    if (size > zone_stats->used || size > tag_stats->used) {
        return false;
    }

    if (count_reserved && size > zone_stats->reserved) {
        return false;
    }

    zone_stats->used -= size;
    tag_stats->used -= size;

    if (count_reserved) {
        zone_stats->reserved -= size;
    }

    if (count_free) {
        ++zone_stats->free_count;
        ++tag_stats->free_count;
    }

    return true;
}

/**
 * @brief  初始化 SDRAM meminfo 统计表
 * @retval None
 * @note   - 本函数会清零 zone/tag 统计并按内存布局表重建 total_sdram
 * @note   - 初始化完成后会把三块固定 framebuffer 记为 reserved/used
 */
void xhgc_meminfo_init(void)
{
    memset(g_xhgc_meminfo_zone_stats, 0, sizeof(g_xhgc_meminfo_zone_stats));
    memset(g_xhgc_meminfo_tag_stats, 0, sizeof(g_xhgc_meminfo_tag_stats));
    g_xhgc_meminfo_total_sdram = 0u;
    g_xhgc_meminfo_total_peak = 0u;

    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        const XHGC_MemZoneDesc *zone = xhgc_mem_get_zone((XHGC_MemZoneId)i);
        if (zone != NULL) {
            g_xhgc_meminfo_zone_stats[i].total = zone->size;
            g_xhgc_meminfo_total_sdram += zone->size;
        }
    }

    g_xhgc_meminfo_initialized = true;

    (void)xhgc_meminfo_reserve(XHGC_MEM_ZONE_LAYER1_FB0,
                               XHGC_MEMINFO_FRAMEBUFFER_SIZE,
                               XHGC_MEM_TAG_FRAMEBUFFER);
    (void)xhgc_meminfo_reserve(XHGC_MEM_ZONE_LAYER1_FB1,
                               XHGC_MEMINFO_FRAMEBUFFER_SIZE,
                               XHGC_MEM_TAG_FRAMEBUFFER);
    (void)xhgc_meminfo_reserve(XHGC_MEM_ZONE_LAYER2_FB0,
                               XHGC_MEMINFO_FRAMEBUFFER_SIZE,
                               XHGC_MEM_TAG_FRAMEBUFFER);
}

/**
 * @brief  记录指定 zone/tag 的保留内存用量
 * @param  zone: 目标内存 zone ID
 * @param  size: 保留字节数
 * @param  tag: 用量归属标签
 * @retval true=记录成功
 * @retval false=meminfo未初始化、参数非法、大小为0或空间不足
 * @note   成功时会同步增加 zone used/reserved、tag used，并更新峰值
 */
bool xhgc_meminfo_reserve(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag)
{
    return xhgc_meminfo_add_usage(zone, size, tag, false, true);
}

/**
 * @brief  释放指定 zone/tag 的保留内存统计
 * @param  zone: 目标内存 zone ID
 * @param  size: 释放字节数
 * @param  tag: 用量归属标签
 * @retval true=释放统计成功
 * @retval false=固定 framebuffer zone、meminfo未初始化、参数非法或统计不足
 * @note   成功时会减少 zone used/reserved 和 tag used，不回退峰值
 */
bool xhgc_meminfo_release(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag)
{
    if (xhgc_meminfo_is_fixed_framebuffer_zone(zone)) {
        return false;
    }

    return xhgc_meminfo_sub_usage(zone, size, tag, false, true);
}

/**
 * @brief  记录一次普通内存分配
 * @param  zone: 目标内存 zone ID
 * @param  size: 分配字节数
 * @param  tag: 用量归属标签
 * @retval true=记录成功
 * @retval false=meminfo未初始化、参数非法、大小为0或空间不足
 * @note   成功时会增加 zone/tag used 和 alloc_count，并更新峰值
 */
bool xhgc_meminfo_alloc_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag)
{
    return xhgc_meminfo_add_usage(zone, size, tag, true, false);
}

/**
 * @brief  记录一次普通内存释放
 * @param  zone: 目标内存 zone ID
 * @param  size: 释放字节数
 * @param  tag: 用量归属标签
 * @retval true=记录成功
 * @retval false=固定 framebuffer zone、meminfo未初始化、参数非法或统计不足
 * @note   成功时会减少 zone/tag used 并增加 free_count，不回退峰值
 */
bool xhgc_meminfo_free_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag)
{
    if (xhgc_meminfo_is_fixed_framebuffer_zone(zone)) {
        return false;
    }

    return xhgc_meminfo_sub_usage(zone, size, tag, true, false);
}

/**
 * @brief  记录一次内存分配失败
 * @param  zone: 目标内存 zone ID
 * @param  size: 失败请求字节数，当前仅用于调用语义标识
 * @param  tag: 用量归属标签
 * @retval true=记录成功
 * @retval false=meminfo未初始化或参数非法
 * @note   成功时只增加 zone/tag fail_count，不修改 used/reserved/peak
 */
bool xhgc_meminfo_fail_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag)
{
    XHGC_MemZoneStats *zone_stats;
    XHGC_MemTagStats *tag_stats;

    (void)size;

    if (!g_xhgc_meminfo_initialized ||
        !xhgc_meminfo_is_valid_zone(zone) ||
        !xhgc_meminfo_is_valid_tag(tag)) {
        return false;
    }

    zone_stats = &g_xhgc_meminfo_zone_stats[zone];
    tag_stats = &g_xhgc_meminfo_tag_stats[tag];
    ++zone_stats->fail_count;
    ++tag_stats->fail_count;
    return true;
}

/**
 * @brief  将指定 zone 中非 reserved 用量按 tag 复位
 * @param  zone: 目标内存 zone ID
 * @param  tag: 要扣减的用量归属标签
 * @retval true=复位成功
 * @retval false=固定 framebuffer zone、meminfo未初始化、参数非法或统计不一致
 * @note   成功时 zone used 回到 reserved，tag used 扣减释放量，峰值和失败计数保持不变
 */
bool xhgc_meminfo_zone_reset_tag(XHGC_MemZoneId zone, XHGC_MemTag tag)
{
    XHGC_MemZoneStats *zone_stats;
    XHGC_MemTagStats *tag_stats;
    uint32_t release_size;

    if (!g_xhgc_meminfo_initialized ||
        !xhgc_meminfo_is_valid_zone(zone) ||
        !xhgc_meminfo_is_valid_tag(tag) ||
        xhgc_meminfo_is_fixed_framebuffer_zone(zone)) {
        return false;
    }

    zone_stats = &g_xhgc_meminfo_zone_stats[zone];
    tag_stats = &g_xhgc_meminfo_tag_stats[tag];

    if (zone_stats->used < zone_stats->reserved) {
        return false;
    }

    release_size = zone_stats->used - zone_stats->reserved;
    if (release_size > tag_stats->used) {
        return false;
    }

    zone_stats->used = zone_stats->reserved;
    tag_stats->used -= release_size;
    return true;
}

/**
 * @brief  复制当前 meminfo 统计快照
 * @param  out: 快照输出结构体
 * @retval None
 * @note   out为NULL时直接返回；本函数不修改统计状态
 */
void xhgc_meminfo_get_snapshot(XHGC_MemInfoSnapshot *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    memcpy(out->zone_stats,
           g_xhgc_meminfo_zone_stats,
           sizeof(g_xhgc_meminfo_zone_stats));
    memcpy(out->tag_stats,
           g_xhgc_meminfo_tag_stats,
           sizeof(g_xhgc_meminfo_tag_stats));
    out->total_sdram = g_xhgc_meminfo_total_sdram;
    out->total_used = xhgc_meminfo_current_total_used();
    out->total_peak = g_xhgc_meminfo_total_peak;
    out->total_fail_count = xhgc_meminfo_current_total_fail_count();
}

/**
 * @brief  打印当前 meminfo 统计信息
 * @retval None
 * @note   本函数通过快照输出 zone/tag/total 统计，不修改统计状态
 */
void xhgc_meminfo_dump(void)
{
    XHGC_MemInfoSnapshot snapshot;

    xhgc_meminfo_get_snapshot(&snapshot);

    printf("[XHGC MEMINFO]\r\n");
    printf("Zones:\r\n");
    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_ZONE_COUNT; ++i) {
        const XHGC_MemZoneDesc *zone = xhgc_mem_get_zone((XHGC_MemZoneId)i);
        const XHGC_MemZoneStats *stats = &snapshot.zone_stats[i];
        const bool fixed = zone != NULL &&
                           (zone->flags & XHGC_MEM_ZONE_FLAG_FIXED) != 0u;
        const bool reserved_future = zone != NULL &&
                                     zone->id == XHGC_MEM_ZONE_SDRAM_LVGL_HEAP;

        printf("  %-16s %s%sused=0x%08lX total=0x%08lX peak=0x%08lX fail=%lu\r\n",
               zone != NULL ? zone->name : "<invalid>",
               fixed ? "fixed " : "",
               reserved_future ? "RESERVED/FUTURE_USE " : "",
               (unsigned long)stats->used,
               (unsigned long)stats->total,
               (unsigned long)stats->peak,
               (unsigned long)stats->fail_count);
    }

    printf("\r\nTags:\r\n");
    for (uint32_t i = 0u; i < (uint32_t)XHGC_MEM_TAG_COUNT; ++i) {
        const XHGC_MemTagStats *stats = &snapshot.tag_stats[i];

        printf("  %-16s used=0x%08lX peak=0x%08lX fail=%lu\r\n",
               g_xhgc_meminfo_tag_names[i],
               (unsigned long)stats->used,
               (unsigned long)stats->peak,
               (unsigned long)stats->fail_count);
    }

    printf("Totals: sdram=0x%08lX used=0x%08lX peak=0x%08lX fail=%lu\r\n",
           (unsigned long)snapshot.total_sdram,
           (unsigned long)snapshot.total_used,
           (unsigned long)snapshot.total_peak,
           (unsigned long)snapshot.total_fail_count);
}

#if defined(__cplusplus)
static_assert(XHGC_MEM_TAG_COUNT == 15, "unexpected XHGC memory tag count");
#else
_Static_assert(XHGC_MEM_TAG_COUNT == 15, "unexpected XHGC memory tag count");
#endif
