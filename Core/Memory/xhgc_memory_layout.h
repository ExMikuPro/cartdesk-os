#ifndef XHGC_MEMORY_LAYOUT_H
#define XHGC_MEMORY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XHGC_SDRAM_BASE          ((uintptr_t)0xD0000000UL)
#define XHGC_SDRAM_SIZE          ((uint32_t)0x04000000UL)
#define XHGC_SDRAM_END_EXCLUSIVE ((uintptr_t)0xD4000000UL)

typedef enum {
    XHGC_MEM_ZONE_LAYER1_FB0 = 0,
    XHGC_MEM_ZONE_LAYER1_FB1,
    XHGC_MEM_ZONE_LAYER2_FB0,
    XHGC_MEM_ZONE_SDRAM_LVGL_HEAP,
    XHGC_MEM_ZONE_DMA_POOL,
    XHGC_MEM_ZONE_LAUNCHER_CACHE,
    XHGC_MEM_ZONE_APP_ARENA_REST,
    XHGC_MEM_ZONE_COUNT
} XHGC_MemZoneId;

typedef enum {
    XHGC_MEM_ZONE_FLAG_SDRAM       = (1u << 0),
    XHGC_MEM_ZONE_FLAG_FIXED       = (1u << 1),
    XHGC_MEM_ZONE_FLAG_FRAMEBUFFER = (1u << 2),
    XHGC_MEM_ZONE_FLAG_LVGL_HEAP   = (1u << 3),
    XHGC_MEM_ZONE_FLAG_DMA         = (1u << 4),
    XHGC_MEM_ZONE_FLAG_CACHE       = (1u << 5),
    XHGC_MEM_ZONE_FLAG_ARENA       = (1u << 6)
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
