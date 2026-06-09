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

/**
 * @brief  初始化SDRAM内存统计表
 * @retval None
 * @note   - 本函数会清空zone/tag统计和全局峰值
 *         - 固定framebuffer zone会按静态大小登记为reserved/used
 */
void xhgc_meminfo_init(void);

/**
 * @brief  记录指定zone/tag的保留内存
 * @param  zone: 内存zone标识
 * @param  size: 保留字节数
 * @param  tag: 内存用途标签
 * @retval true=记录成功, false=参数非法或容量不足
 */
bool xhgc_meminfo_reserve(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);

/**
 * @brief  释放指定zone/tag的保留内存记录
 * @param  zone: 内存zone标识
 * @param  size: 释放字节数
 * @param  tag: 内存用途标签
 * @retval true=记录成功, false=参数非法或统计不足
 * @note   - 固定framebuffer zone不允许通过本函数释放
 */
bool xhgc_meminfo_release(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);

/**
 * @brief  记录一次成功分配
 * @param  zone: 内存zone标识
 * @param  size: 分配消耗字节数
 * @param  tag: 内存用途标签
 * @retval true=记录成功, false=参数非法或容量不足
 * @note   - 本函数会更新zone/tag的used、alloc_count和peak统计
 */
bool xhgc_meminfo_alloc_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);

/**
 * @brief  记录一次释放
 * @param  zone: 内存zone标识
 * @param  size: 释放字节数
 * @param  tag: 内存用途标签
 * @retval true=记录成功, false=参数非法或统计不足
 * @note   - 固定framebuffer zone不允许通过本函数释放
 */
bool xhgc_meminfo_free_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);

/**
 * @brief  记录一次分配失败
 * @param  zone: 内存zone标识
 * @param  size: 申请字节数
 * @param  tag: 内存用途标签
 * @retval true=记录成功, false=参数非法或meminfo未初始化
 * @note   - 当前实现仅增加fail_count，不保存失败size
 */
bool xhgc_meminfo_fail_record(XHGC_MemZoneId zone, uint32_t size, XHGC_MemTag tag);

/**
 * @brief  将指定zone中指定tag的动态used统计重置到reserved基线
 * @param  zone: 内存zone标识
 * @param  tag: 内存用途标签
 * @retval true=重置成功, false=参数非法或统计状态不一致
 * @note   - 固定framebuffer zone不允许通过本函数重置
 */
bool xhgc_meminfo_zone_reset_tag(XHGC_MemZoneId zone, XHGC_MemTag tag);

/**
 * @brief  获取当前内存统计快照
 * @param  out: 输出快照指针
 * @retval None
 * @note   - out为NULL时不执行任何操作
 */
void xhgc_meminfo_get_snapshot(XHGC_MemInfoSnapshot *out);

/**
 * @brief  打印当前内存统计信息
 * @retval None
 * @note   - 输出包含zone统计、tag统计和全局汇总
 */
void xhgc_meminfo_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_MEMINFO_H */
