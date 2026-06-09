#ifndef __SDRAM_H
#define __SDRAM_H

#include "main.h"
#include "sdram_layout.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern SDRAM_HandleTypeDef hsdram1;

#define FMC_SDRAM_ADDR   ((uint32_t)(SDRAM_BASE_ADDR)) /* SDRAM base address */

#define EXT_SDRAM_SIZE   ((uint32_t)SDRAM_TOTAL_SIZE)


/* SDRAM���ò��� */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

/**
 * @brief  执行FMC SDRAM器件初始化命令序列
 * @retval None
 * @note   - 依赖CubeMX已完成FMC/SDRAM控制器和hsdram1初始化
 *         - 会发送CLK_ENABLE、PALL、AUTOREFRESH、LOAD_MODE并设置refresh rate
 */
void SDRAM_Init(void);

/**
 * @brief  校验固定SDRAM分区布局和APP_ARENA_REST内部拆分
 * @retval None
 * @note   - 检查失败会进入Error_Handler
 *         - LUA_HEAP独立保留，RESOURCE_ARENA reset不会回收LUA_HEAP
 */
void sdram_layout_check(void);

/**
 * @brief  按字节写入SDRAM
 * @param  pBuffer: 源数据缓冲区
 * @param  WriteAddr: 相对FMC_SDRAM_ADDR的写入偏移
 * @param  n: 写入字节数
 * @retval None
 */
void FMC_SDRAM_Write_Buffer(uint8_t *pBuffer, uint32_t WriteAddr, uint32_t n);

/**
 * @brief  按字节读取SDRAM
 * @param  pBuffer: 目标数据缓冲区
 * @param  ReadAddr: 相对FMC_SDRAM_ADDR的读取偏移
 * @param  n: 读取字节数
 * @retval None
 */
void FMC_SDRAM_Read_Buffer(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t n);

/**
 * @brief  执行SDRAM容量探测测试
 * @retval None
 * @note   - 本函数会向SDRAM写入测试模式并打印探测容量
 */
void FMC_SDRAM_Test(void);

/**
 * @brief  执行SDRAM写入速度测试
 * @retval None
 * @note   - 本函数会覆盖FMC_SDRAM_ADDR起始区域的数据
 */
void SDRAM_WriteSpeed_Test(void);

/**
 * @brief  执行SDRAM读取速度测试
 * @retval None
 * @note   - 本函数从FMC_SDRAM_ADDR起始区域读取数据并打印耗时
 */
void SDRAM_ReadSpeed_Test(void);

/**
 * @brief  执行SDRAM最小读写测试
 * @retval 0=成功, 非0=失败
 * @note   - TODO: 返回值语义需结合实现文件进一步确认
 */
int SDRAM_MinTest(void);

/**
 * @brief  初始化DMA_POOL线性分配器
 * @retval None
 * @note   - 会清零DMA_POOL offset和peak
 *         - 会重置DMA标签的meminfo used统计
 */
void SDRAM_DmaPoolInit(void);

/**
 * @brief  重置DMA_POOL线性分配器
 * @retval None
 * @note   - 会清零DMA_POOL offset但保留本地peak统计
 *         - 会重置DMA标签的meminfo used统计
 */
void SDRAM_DmaPoolReset(void);

/**
 * @brief  从DMA_POOL分配一块对齐内存
 * @param  size: 申请字节数
 * @param  align: 请求对齐字节数，会提升到至少SDRAM_DMA_ALIGN
 * @return 非NULL=分配成功返回指针, NULL=参数非法或空间不足
 * @note   - DMA_POOL是reset-only线性分配器，不支持单块释放
 *         - 成功/失败路径会同步更新meminfo统计
 */
void *SDRAM_DmaPoolAlloc(size_t size, size_t align);

/**
 * @brief  从DMA_POOL分配并清零一块对齐内存
 * @param  count: 元素数量
 * @param  size: 单个元素字节数
 * @param  align: 请求对齐字节数，会提升到至少SDRAM_DMA_ALIGN
 * @return 非NULL=分配成功返回已清零指针, NULL=参数非法、乘法溢出或空间不足
 * @note   - 清零由CPU执行，调用方如交给DMA读取仍需处理DCache clean
 */
void *SDRAM_DmaPoolCalloc(size_t count, size_t size, size_t align);

/**
 * @brief  查询DMA_POOL已使用字节数
 * @return 已使用字节数，超过uint32_t上限时返回UINT32_MAX
 */
uint32_t SDRAM_DmaPoolUsed(void);

/**
 * @brief  查询DMA_POOL历史峰值字节数
 * @return 峰值字节数，超过uint32_t上限时返回UINT32_MAX
 */
uint32_t SDRAM_DmaPoolPeak(void);

/**
 * @brief  查询DMA_POOL剩余字节数
 * @return 当前剩余字节数，zone非法或已满时返回0
 */
uint32_t SDRAM_DmaPoolFree(void);

/**
 * @brief  判断地址范围是否完全位于DMA_POOL内
 * @param  ptr: 起始地址指针
 * @param  size: 范围字节数
 * @retval true=范围完全位于DMA_POOL内, false=参数非法或范围越界
 */
bool SDRAM_DmaPoolContains(const void *ptr, size_t size);

#if XHGC_DMA_POOL_SELFTEST_ENABLE
/**
 * @brief  执行DMA_POOL分配器和meminfo联动自检
 * @retval true=自检通过, false=自检失败
 * @note   - 本函数会重置DMA_POOL并执行多次分配、越界和reset检查
 */
bool SDRAM_DmaPoolSelftest(void);
#endif

/**
 * @brief  从RESOURCE_ARENA分配一块对齐内存
 * @param  size: 申请字节数
 * @param  align: 请求对齐字节数，会提升到至少SDRAM_DEFAULT_ALIGN
 * @return 非NULL=分配成功返回指针, NULL=参数非法或空间不足
 * @note   - 这是保留兼容命名的API，实际只操作RESOURCE_ARENA
 */
void *SDRAM_AppArenaAlloc(size_t size, size_t align);

/**
 * @brief  重置RESOURCE_ARENA线性分配器
 * @retval None
 * @note   - 本函数不会回收LUA_HEAP
 *         - 若RESOURCE_ARENA边界校验失败会进入Error_Handler
 */
void SDRAM_AppArenaReset(void);

/**
 * @brief  查询RESOURCE_ARENA已使用字节数
 * @return 当前已使用字节数
 */
size_t SDRAM_AppArenaUsed(void);

/**
 * @brief  查询RESOURCE_ARENA剩余字节数
 * @return 当前剩余字节数
 */
size_t SDRAM_AppArenaFree(void);

#endif
