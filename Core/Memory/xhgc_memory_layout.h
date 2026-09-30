#ifndef XHGC_MEMORY_LAYOUT_H
#define XHGC_MEMORY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdram_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SDRAM 固定分区元数据层。
 *
 * 所有 base / size / end 均来自 Core/Inc/sdram_layout.h（统一布局），本文件
 * 只负责给分区附加 zone id、名字和 flags，供 meminfo / dcache / 调试使用。
 * 禁止在本文件写绝对地址。
 */

#define XHGC_SDRAM_BASE          SDRAM_BASE_ADDR
#define XHGC_SDRAM_SIZE          SDRAM_TOTAL_SIZE
#define XHGC_SDRAM_END_EXCLUSIVE SDRAM_LIMIT_ADDR

typedef enum {
    XHGC_MEM_ZONE_LAYER0_FB = 0,
    XHGC_MEM_ZONE_LVGL_FB_A,
    XHGC_MEM_ZONE_LVGL_FB_B,
    XHGC_MEM_ZONE_LAUNCHER_STRIP,
    XHGC_MEM_ZONE_SDRAM_LVGL_HEAP,
    XHGC_MEM_ZONE_DMA_POOL,
    XHGC_MEM_ZONE_LAUNCHER_CACHE,
    XHGC_MEM_ZONE_APP_ARENA_REST,
    XHGC_MEM_ZONE_COUNT
} XHGC_MemZoneId;

typedef enum {
    XHGC_MEM_ZONE_FLAG_SDRAM          = (1u << 0),
    XHGC_MEM_ZONE_FLAG_FIXED          = (1u << 1),
    XHGC_MEM_ZONE_FLAG_FRAMEBUFFER    = (1u << 2),
    XHGC_MEM_ZONE_FLAG_LVGL_HEAP      = (1u << 3),
    XHGC_MEM_ZONE_FLAG_DMA            = (1u << 4),
    XHGC_MEM_ZONE_FLAG_CACHE          = (1u << 5),
    XHGC_MEM_ZONE_FLAG_ARENA          = (1u << 6),
    XHGC_MEM_ZONE_FLAG_LAUNCHER_STRIP = (1u << 7)
} XHGC_MemZoneFlags;

typedef struct {
    XHGC_MemZoneId id;
    const char *name;
    uintptr_t base;
    uint32_t size;
    uintptr_t end;
    uint32_t flags;
} XHGC_MemZoneDesc;

extern const XHGC_MemZoneDesc g_xhgc_mem_zones[XHGC_MEM_ZONE_COUNT];

/**
 * @brief  按zone id获取SDRAM固定分区描述
 * @param  id: 内存zone标识
 * @return 非NULL=分区描述指针, NULL=id非法
 */
const XHGC_MemZoneDesc* xhgc_mem_get_zone(XHGC_MemZoneId id);

/**
 * @brief  按地址查找所属SDRAM固定分区
 * @param  addr: 待查询地址
 * @return 非NULL=命中的分区描述指针, NULL=地址不在任何分区内
 */
const XHGC_MemZoneDesc* xhgc_mem_find_zone_by_addr(uintptr_t addr);

/**
 * @brief  判断地址范围是否完全位于指定zone内
 * @param  id: 内存zone标识
 * @param  addr: 起始地址
 * @param  size: 范围字节数
 * @retval true=范围完全位于zone内, false=参数非法或范围越界
 */
bool xhgc_mem_addr_in_zone(XHGC_MemZoneId id, uintptr_t addr, uint32_t size);

/**
 * @brief  判断地址范围是否属于固定DMA目标区域
 * @param  ptr: 起始地址指针
 * @param  size: 范围字节数
 * @retval true=属于固定DMA目标区域, false=参数非法或不属于
 * @note   - 固定DMA目标包含framebuffer、LAUNCHER_CACHE和APP_ARENA_REST
 */
bool xhgc_mem_is_fixed_dma_target(const void *ptr, size_t size);

/**
 * @brief  校验SDRAM固定分区布局是否合法
 * @retval true=布局合法, false=布局不合法
 * @note   - 检查SDRAM基址/大小/结束地址、分区顺序、紧贴关系和对齐要求
 *         - 本函数不初始化SDRAM，只验证静态zone table
 */
bool xhgc_mem_layout_validate(void);

/**
 * @brief  打印SDRAM固定分区布局
 * @retval None
 * @note   - 输出每个zone的base、size、end和flags
 */
void xhgc_mem_layout_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_MEMORY_LAYOUT_H */
