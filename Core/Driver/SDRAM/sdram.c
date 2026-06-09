#include "sdram.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "xhgc_meminfo.h"
#include "xhgc_memory_layout.h"

uint32_t timer;
uint32_t write_timer = 0, read_time = 0;

static size_t s_dma_pool_offset = 0;
static size_t s_dma_pool_peak = 0;
static size_t s_resource_arena_offset = 0;

static int sdram_range_tightly_follows(uintptr_t prev_end, uintptr_t next_base)
{
    return (prev_end + 1UL) == next_base;
}

static int sdram_range_aligned(uintptr_t base, uintptr_t end, size_t align)
{
    return ((base % align) == 0u) && (((end + 1UL) % align) == 0u);
}

static bool sdram_size_add_overflows(size_t a, size_t b)
{
    return a > (SIZE_MAX - b);
}

static bool sdram_size_mul_overflows(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return true;
    }

    if (a != 0u && b > (SIZE_MAX / a)) {
        return true;
    }

    *out = a * b;
    return false;
}

static bool sdram_align_is_power_of_two(size_t align)
{
    return align != 0u && (align & (align - 1u)) == 0u;
}

/**
 * @brief  修正线性分配请求的对齐值
 * @param  requested_align: 调用方请求的对齐字节数
 * @param  min_align: 允许的最小对齐字节数
 * @param  out_align: 输出修正后的对齐字节数
 * @retval true=修正成功, false=输出指针非法或对齐值上修时溢出
 * @note   - 小于min_align的请求会提升到min_align
 *         - 非2的幂对齐值会向上修正到不小于请求值的2的幂
 *         - 本函数只处理数值，不推进任何pool offset
 */
static bool sdram_align_normalize(size_t requested_align, size_t min_align, size_t *out_align)
{
    size_t align;

    if (out_align == NULL) {
        return false;
    }

    align = requested_align;
    if (align < min_align) {
        align = min_align;
    }

    if (!sdram_align_is_power_of_two(align)) {
        size_t fixed = min_align;
        while (fixed < align) {
            if (fixed > (SIZE_MAX >> 1u)) {
                return false;
            }
            fixed <<= 1u;
        }
        align = fixed;
    }

    *out_align = align;
    return true;
}

/**
 * @brief  获取并校验DMA_POOL对应的固定内存zone描述
 * @retval 非NULL=DMA_POOL zone合法, NULL=zone缺失、非DMA区或边界定义不一致
 * @note   - 本函数只读全局内存布局表，不初始化SDRAM
 *         - 调用方依赖返回值决定是否允许DMA_POOL分配或contains判断
 */
static const XHGC_MemZoneDesc *sdram_dma_pool_zone(void)
{
    const XHGC_MemZoneDesc *zone = xhgc_mem_get_zone(XHGC_MEM_ZONE_DMA_POOL);

    if (zone == NULL ||
        (zone->flags & XHGC_MEM_ZONE_FLAG_DMA) == 0u ||
        zone->base >= zone->end ||
        zone->size != (uint32_t)(zone->end - zone->base)) {
        return NULL;
    }

    return zone;
}

/**
 * @brief  记录一次DMA_POOL分配失败
 * @param  size: 失败的申请字节数
 * @retval None
 * @note   - 本函数只更新meminfo fail_count，不修改pool offset或peak
 *         - 超过32位的申请大小会按UINT32_MAX上报统计
 */
static void sdram_dma_pool_record_fail(size_t size)
{
    (void)xhgc_meminfo_fail_record(XHGC_MEM_ZONE_DMA_POOL,
                                   size > UINT32_MAX ? UINT32_MAX : (uint32_t)size,
                                   XHGC_MEM_TAG_DMA);
}

/**
 * @brief  从DMA_POOL线性分配一段cache line安全的DMA内存
 * @param  size: 申请字节数
 * @param  align: 调用方请求的对齐字节数
 * @return 非NULL=分配成功的对齐地址, NULL=参数非法、布局非法、溢出或空间不足
 * @note   - 本函数会推进s_dma_pool_offset并刷新s_dma_pool_peak
 *         - DMA_POOL不支持单块free，只能通过SDRAM_DmaPoolReset整体复位
 *         - meminfo按实际消耗字节数记录，包含对齐填充带来的pool占用
 *         - 失败路径只记录fail_count，不改变offset/peak
 */
static void *sdram_dma_pool_alloc_internal(size_t size, size_t align)
{
    const XHGC_MemZoneDesc *zone;
    uintptr_t current;
    uintptr_t aligned;
    size_t new_offset;
    size_t consumed;

    if (size == 0u || !sdram_align_normalize(align, SDRAM_DMA_ALIGN, &align)) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    zone = sdram_dma_pool_zone();
    if (zone == NULL || s_dma_pool_offset > zone->size) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    if (sdram_size_add_overflows(zone->base, s_dma_pool_offset)) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    current = zone->base + s_dma_pool_offset;
    aligned = sdram_align_up_uintptr(current, align);
    if (aligned < current || aligned < zone->base) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    new_offset = (size_t)(aligned - zone->base);
    if (new_offset > zone->size || size > (zone->size - new_offset)) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    consumed = (new_offset + size) - s_dma_pool_offset;
    if (consumed > UINT32_MAX) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    s_dma_pool_offset = new_offset + size;
    if (s_dma_pool_offset > s_dma_pool_peak) {
        s_dma_pool_peak = s_dma_pool_offset;
    }

    (void)xhgc_meminfo_alloc_record(XHGC_MEM_ZONE_DMA_POOL,
                                    (uint32_t)consumed,
                                    XHGC_MEM_TAG_DMA);
    return (void *)aligned;
}

/**
 * @brief  在指定SDRAM线性区域内推进offset并返回对齐内存
 * @param  base: 线性区域起始地址
 * @param  capacity: 线性区域总容量
 * @param  offset: 输入/输出当前分配偏移
 * @param  size: 申请字节数
 * @param  align: 调用方请求的对齐字节数
 * @return 非NULL=分配成功的对齐地址, NULL=参数非法、对齐非法或容量不足
 * @note   - 本函数只推进调用方传入的offset，不维护peak/fail统计
 *         - 不支持单块释放；调用方需通过所属arena/pool reset回收
 *         - 调用方必须保证base+offset未发生地址回绕
 */
static void *sdram_linear_alloc(uintptr_t base, size_t capacity, size_t *offset, size_t size, size_t align)
{
    uintptr_t current;
    uintptr_t aligned;
    size_t new_offset;

    if (size == 0u) {
        return NULL;
    }

    if (align < SDRAM_DEFAULT_ALIGN) {
        align = SDRAM_DEFAULT_ALIGN;
    }

    if ((align & (align - 1u)) != 0u) {
        return NULL;
    }

    current = base + *offset;
    aligned = sdram_align_up_uintptr(current, align);
    new_offset = (size_t)(aligned - base);

    if (new_offset > capacity || size > (capacity - new_offset)) {
        return NULL;
    }

    *offset = new_offset + size;
    return (void *)aligned;
}

/**
 * @brief  校验 SDRAM 编译期布局常量的边界和连续性
 * @retval None
 * @note   检查失败会调用 Error_Handler；本函数不修改 SDRAM 初始化顺序
 */
void sdram_layout_check(void)
{
    if (SDRAM_BASE_ADDR != SDRAM_LAYER1_FB0_BASE ||
        SDRAM_END_ADDR != SDRAM_APP_ARENA_END ||
        SDRAM_TOTAL_SIZE != (uint32_t)(SDRAM_END_ADDR - SDRAM_BASE_ADDR + 1UL)) {
        Error_Handler();
    }

    if (!sdram_range_tightly_follows(SDRAM_LAYER1_FB0_END, SDRAM_LAYER1_FB1_BASE) ||
        !sdram_range_tightly_follows(SDRAM_LAYER1_FB1_END, SDRAM_LAYER2_FB0_BASE) ||
        !sdram_range_tightly_follows(SDRAM_LAYER2_FB0_END, SDRAM_LVGL_HEAP_BASE) ||
        !sdram_range_tightly_follows(SDRAM_LVGL_HEAP_END, SDRAM_DMA_POOL_BASE) ||
        !sdram_range_tightly_follows(SDRAM_DMA_POOL_END, SDRAM_LAUNCHER_CACHE_BASE) ||
        !sdram_range_tightly_follows(SDRAM_LAUNCHER_CACHE_END, SDRAM_APP_ARENA_BASE)) {
        Error_Handler();
    }

    if (!sdram_range_aligned(SDRAM_LAYER1_FB0_BASE, SDRAM_LAYER1_FB0_END, SDRAM_FB_ALIGN) ||
        !sdram_range_aligned(SDRAM_LAYER1_FB1_BASE, SDRAM_LAYER1_FB1_END, SDRAM_FB_ALIGN) ||
        !sdram_range_aligned(SDRAM_LAYER2_FB0_BASE, SDRAM_LAYER2_FB0_END, SDRAM_FB_ALIGN)) {
        Error_Handler();
    }

    if (!sdram_range_aligned(SDRAM_DMA_POOL_BASE, SDRAM_DMA_POOL_END, SDRAM_DMA_ALIGN)) {
        Error_Handler();
    }

    if (!sdram_range_aligned(SDRAM_LVGL_HEAP_BASE, SDRAM_LVGL_HEAP_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(SDRAM_LAUNCHER_CACHE_BASE, SDRAM_LAUNCHER_CACHE_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(SDRAM_APP_ARENA_BASE, SDRAM_APP_ARENA_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(LUA_HEAP_BASE, LUA_HEAP_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(RESOURCE_ARENA_BASE, RESOURCE_ARENA_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(COLD_POOL_BASE, COLD_POOL_END, SDRAM_DEFAULT_ALIGN)) {
        Error_Handler();
    }

    if (!sdram_addr_in_app_arena(LUA_HEAP_BASE) ||
        !sdram_addr_in_app_arena(LUA_HEAP_END) ||
        !sdram_addr_in_app_arena(RESOURCE_ARENA_BASE) ||
        !sdram_addr_in_app_arena(RESOURCE_ARENA_END) ||
        !sdram_addr_in_app_arena(COLD_POOL_BASE) ||
        !sdram_addr_in_app_arena(COLD_POOL_END) ||
        !sdram_range_tightly_follows(LUA_HEAP_END, RESOURCE_ARENA_BASE) ||
        RESOURCE_ARENA_END >= COLD_POOL_BASE) {
        Error_Handler();
    }
}

/**
 * @brief  按 JEDEC 时序初始化外部 SDRAM
 * @retval None
 * @note   本函数依次发送 CLK_ENABLE、PALL、AUTOREFRESH、LOAD_MODE 并配置刷新率
 */
void SDRAM_Init(void)
{
    FMC_SDRAM_CommandTypeDef Command = {0};

    Command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    Command.AutoRefreshNumber = 1;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0xFFFF);

    HAL_Delay(1);

    Command.CommandMode = FMC_SDRAM_CMD_PALL;
    Command.AutoRefreshNumber = 1;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0xFFFF);

    Command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command.AutoRefreshNumber = 8;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0xFFFF);

    __IO uint32_t temp =
        SDRAM_MODEREG_BURST_LENGTH_4 |
        SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
        SDRAM_MODEREG_CAS_LATENCY_3 |
        SDRAM_MODEREG_OPERATING_MODE_STANDARD |
        SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    Command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    Command.AutoRefreshNumber = 1;
    Command.ModeRegisterDefinition = temp;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0xFFFF);

    // ★ 139MHz 下的 refresh
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, 918);
}


/**
 * @brief  将缓冲区逐字节写入 SDRAM
 * @param  pBuffer: 源数据缓冲区
 * @param  WriteAddr: 相对 FMC_SDRAM_ADDR 的写入偏移
 * @param  n: 写入字节数
 * @retval None
 */
void FMC_SDRAM_Write_Buffer(uint8_t *pBuffer, uint32_t WriteAddr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *(__IO uint8_t*)(FMC_SDRAM_ADDR + WriteAddr) = *pBuffer;
        WriteAddr++;
        pBuffer++;
    }
}

/**
 * @brief  从 SDRAM 逐字节读取到缓冲区
 * @param  pBuffer: 目标数据缓冲区
 * @param  ReadAddr: 相对 FMC_SDRAM_ADDR 的读取偏移
 * @param  n: 读取字节数
 * @retval None
 */
void FMC_SDRAM_Read_Buffer(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *pBuffer++ = *(__IO uint8_t*)(FMC_SDRAM_ADDR + ReadAddr);
        ReadAddr++;
    }
}

/**
 * @brief  对 SDRAM 执行基础容量写读测试
 * @retval None
 * @note   本函数会向 SDRAM 采样地址写入测试值并通过 printf 输出容量信息
 */
void FMC_SDRAM_Test(void)
{
    uint32_t i = 0, temp = 0, sval = 0;

    /* Write test pattern every 16KB, total 4096 patterns for 64MB */
    for (i = 0; i < 64 * 1024 * 1024; i += 16 * 1024)
    {
        *(__IO uint32_t*)(FMC_SDRAM_ADDR + i) = temp;
        temp++;
    }

    /* Read back and verify */
    for (i = 0; i < 64 * 1024 * 1024; i += 16 * 1024)
    {
        temp = *(uint32_t*)(FMC_SDRAM_ADDR + i);
        if (i == 0)
            sval = temp;
        else if (temp <= sval)
            break; /* Each value should be greater than previous */

        printf("SDRAM Capacity: %dKB\r\n", (uint16_t)(temp - sval + 1) * 16);
    }
}

/**
 * @brief  SDRAM check failed handler
 * @param  None
 * @retval None
 */
static void SDRAM_Checkfailed(void)
{
    printf("SDRAM ERROR\r\n");
    while (1);
}

/**
 * @brief  执行 SDRAM 写入速度测试
 * @retval None
 * @note   本函数会覆盖 FMC_SDRAM_ADDR 起始区域并在校验失败时停入错误处理
 */
void SDRAM_WriteSpeed_Test(void)
{
    uint32_t i, j = 0;
    uint32_t *pBuf;

    pBuf = (uint32_t *)FMC_SDRAM_ADDR;
    timer = 0;

    /* Write data to SDRAM in unrolled loop for speed */
    for (i = 0; i < 2 * 1024 * 1024 / 4; i++)
    {
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
        *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;  *pBuf++ = j++;
    }

    write_timer = timer; /* Save elapsed time */

    /* Verify written data */
    j = 0;
    pBuf = (uint32_t *)FMC_SDRAM_ADDR;
    for (i = 0; i < 2 * 1024 * 1024 * 8; i++)
    {
        if (*pBuf++ != j++)
        {
            printf("Write error at j=%d\r\n", j);
            SDRAM_Checkfailed();
        }
    }

    printf("64MB write time: %dms, write speed: %dMB/s\r\n",
           write_timer, (EXT_SDRAM_SIZE / 1024 / 1024 * 1000) / write_timer);
}

/**
 * @brief  执行 SDRAM 读取速度测试
 * @retval None
 * @note   本函数从 FMC_SDRAM_ADDR 起始区域连续读取并输出耗时统计
 */
void SDRAM_ReadSpeed_Test(void)
{
    uint32_t i;
    uint32_t *pBuf;
    __IO uint32_t ulTemp; /* Volatile to prevent compiler optimization */

    pBuf = (uint32_t *)FMC_SDRAM_ADDR;
    timer = 0;

    /* Read all SDRAM space */
    for (i = 0; i < 2 * 1024 * 1024 / 4; i++)
    {
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
        ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;  ulTemp = *pBuf++;
    }

    read_time = timer; /* Save elapsed time */

    printf("64MB read time: %dms, read speed: %dMB/s\r\n\r\n",
           read_time, (EXT_SDRAM_SIZE / 1024 / 1024 * 1000) / read_time);
}

/**
 * @brief  从 DMA_POOL 线性分配一块对齐内存
 * @param  size: 申请字节数
 * @param  align: 请求对齐字节数，小于 SDRAM_DMA_ALIGN 时会提升到 SDRAM_DMA_ALIGN
 * @return 非NULL=分配成功, NULL=参数非法、对齐失败、地址溢出或空间不足
 * @note   - 本函数使用 bump/reset 模型，不支持单块释放
 * @note   - 成功/失败路径会同步更新 meminfo 中 DMA tag 统计
 * @note   - 返回地址一定位于 DMA_POOL zone 内
 */
void *SDRAM_DmaPoolAlloc(size_t size, size_t align)
{
    return sdram_dma_pool_alloc_internal(size, align);
}

/**
 * @brief  从 DMA_POOL 分配并清零一块数组内存
 * @param  count: 元素数量
 * @param  size: 单个元素字节数
 * @param  align: 请求对齐字节数，小于 SDRAM_DMA_ALIGN 时会提升到 SDRAM_DMA_ALIGN
 * @return 非NULL=分配并清零成功, NULL=乘法溢出、参数非法或空间不足
 * @note   本函数沿用 DMA_POOL bump/reset 模型，失败路径会记录 meminfo fail_count
 */
void *SDRAM_DmaPoolCalloc(size_t count, size_t size, size_t align)
{
    size_t total_size;
    void *ptr;

    if (sdram_size_mul_overflows(count, size, &total_size)) {
        sdram_dma_pool_record_fail(size);
        return NULL;
    }

    ptr = SDRAM_DmaPoolAlloc(total_size, align);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

/**
 * @brief  初始化 DMA_POOL 线性分配器
 * @retval None
 * @note   本函数会清零当前用量和峰值，并复位 meminfo 中 DMA_POOL/DMA tag 的非 reserved 用量
 */
void SDRAM_DmaPoolInit(void)
{
    s_dma_pool_offset = 0u;
    s_dma_pool_peak = 0u;
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_DMA_POOL, XHGC_MEM_TAG_DMA);
}

/**
 * @brief  复位 DMA_POOL 当前分配位置
 * @retval None
 * @note   本函数清零当前 offset 并复位 meminfo 用量，但保留本地 peak 统计
 */
void SDRAM_DmaPoolReset(void)
{
    s_dma_pool_offset = 0u;
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_DMA_POOL, XHGC_MEM_TAG_DMA);
}

/**
 * @brief  获取 DMA_POOL 当前已消耗字节数
 * @retval 当前已用字节数，超过 uint32_t 上限时返回 UINT32_MAX
 */
uint32_t SDRAM_DmaPoolUsed(void)
{
    return s_dma_pool_offset > UINT32_MAX ? UINT32_MAX : (uint32_t)s_dma_pool_offset;
}

/**
 * @brief  获取 DMA_POOL 历史峰值字节数
 * @retval 峰值字节数，超过 uint32_t 上限时返回 UINT32_MAX
 */
uint32_t SDRAM_DmaPoolPeak(void)
{
    return s_dma_pool_peak > UINT32_MAX ? UINT32_MAX : (uint32_t)s_dma_pool_peak;
}

/**
 * @brief  获取 DMA_POOL 当前剩余字节数
 * @retval 剩余字节数，zone非法或已耗尽时返回0
 */
uint32_t SDRAM_DmaPoolFree(void)
{
    const XHGC_MemZoneDesc *zone = sdram_dma_pool_zone();

    if (zone == NULL || s_dma_pool_offset >= zone->size) {
        return 0u;
    }

    return (uint32_t)(zone->size - s_dma_pool_offset);
}

/**
 * @brief  判断地址范围是否完整位于 DMA_POOL 内
 * @param  ptr: 起始地址
 * @param  size: 范围字节数
 * @retval true=范围完整位于 DMA_POOL 内
 * @retval false=zone非法、参数非法、地址溢出或范围越界
 */
bool SDRAM_DmaPoolContains(const void *ptr, size_t size)
{
    const XHGC_MemZoneDesc *zone = sdram_dma_pool_zone();
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;

    if (zone == NULL || ptr == NULL || size == 0u || size > (SIZE_MAX - start)) {
        return false;
    }

    end = start + size;
    return start >= zone->base && end <= zone->end;
}

#if XHGC_DMA_POOL_SELFTEST_ENABLE
static void sdram_dma_pool_selftest_expect(bool condition, bool *pass, const char *label)
{
    if (condition) {
        printf("[XHGC DMA_POOL SELFTEST] PASS %s\r\n", label);
    } else {
        printf("[XHGC DMA_POOL SELFTEST] FAIL %s\r\n", label);
        if (pass != NULL) {
            *pass = false;
        }
    }
}

/**
 * @brief  执行 DMA_POOL 分配器与 meminfo 联动自检
 * @retval true=所有自检项通过
 * @retval false=至少一个自检项失败
 * @note   本函数会调用 SDRAM_DmaPoolInit/Reset，改变 DMA_POOL 当前分配状态
 */
bool SDRAM_DmaPoolSelftest(void)
{
    const XHGC_MemZoneDesc *zone;
    XHGC_MemInfoSnapshot baseline;
    XHGC_MemInfoSnapshot after_4k;
    XHGC_MemInfoSnapshot after_128k;
    XHGC_MemInfoSnapshot before_fail;
    XHGC_MemInfoSnapshot after_fail;
    XHGC_MemInfoSnapshot after_reset;
    void *p4k;
    void *p128k;
    void *pfail;
    uint32_t local_peak_before_reset;
    bool pass = true;

    printf("[XHGC DMA_POOL SELFTEST] begin\r\n");
    SDRAM_DmaPoolInit();
    printf("[XHGC DMA_POOL SELFTEST] baseline\r\n");
    xhgc_meminfo_dump();
    xhgc_meminfo_get_snapshot(&baseline);

    p4k = SDRAM_DmaPoolAlloc(4096u, 64u);
    xhgc_meminfo_get_snapshot(&after_4k);
    sdram_dma_pool_selftest_expect(p4k != NULL, &pass, "alloc 4096");
    sdram_dma_pool_selftest_expect(SDRAM_DmaPoolContains(p4k, 4096u), &pass, "alloc 4096 contains");
    sdram_dma_pool_selftest_expect((((uintptr_t)p4k) & 63u) == 0u, &pass, "alloc 4096 align64");
    sdram_dma_pool_selftest_expect(after_4k.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used >
                                   baseline.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used,
                                   &pass, "meminfo used after 4096");

    p128k = SDRAM_DmaPoolAlloc(128u * 1024u, 256u);
    xhgc_meminfo_get_snapshot(&after_128k);
    sdram_dma_pool_selftest_expect(p128k != NULL, &pass, "alloc 128KiB");
    sdram_dma_pool_selftest_expect(SDRAM_DmaPoolContains(p128k, 128u * 1024u), &pass, "alloc 128KiB contains");
    sdram_dma_pool_selftest_expect((((uintptr_t)p128k) & 255u) == 0u, &pass, "alloc 128KiB align256");
    sdram_dma_pool_selftest_expect(after_128k.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used >
                                   after_4k.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used,
                                   &pass, "meminfo used after 128KiB");

    xhgc_meminfo_get_snapshot(&before_fail);
    pfail = SDRAM_DmaPoolAlloc((size_t)SDRAM_DmaPoolFree() + 1u, 64u);
    xhgc_meminfo_get_snapshot(&after_fail);
    sdram_dma_pool_selftest_expect(pfail == NULL, &pass, "oversize returns NULL");
    sdram_dma_pool_selftest_expect(after_fail.zone_stats[XHGC_MEM_ZONE_DMA_POOL].fail_count >
                                   before_fail.zone_stats[XHGC_MEM_ZONE_DMA_POOL].fail_count,
                                   &pass, "oversize fail_count");

    zone = sdram_dma_pool_zone();
    sdram_dma_pool_selftest_expect(zone != NULL, &pass, "zone table");
    if (zone != NULL) {
        sdram_dma_pool_selftest_expect(SDRAM_DmaPoolContains((const void *)zone->base, 1u),
                                       &pass, "contains base");
        sdram_dma_pool_selftest_expect(SDRAM_DmaPoolContains((const void *)(zone->end - 1u), 1u),
                                       &pass, "contains end-1");
        sdram_dma_pool_selftest_expect(!SDRAM_DmaPoolContains((const void *)zone->end, 1u),
                                       &pass, "reject end");
        sdram_dma_pool_selftest_expect(!SDRAM_DmaPoolContains((const void *)(zone->end - 1u), 2u),
                                       &pass, "reject crossing end");
    }

    local_peak_before_reset = SDRAM_DmaPoolPeak();
    SDRAM_DmaPoolReset();
    xhgc_meminfo_get_snapshot(&after_reset);
    sdram_dma_pool_selftest_expect(SDRAM_DmaPoolUsed() == 0u, &pass, "reset local used");
    sdram_dma_pool_selftest_expect(after_reset.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used ==
                                   baseline.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used,
                                   &pass, "reset meminfo used");
    sdram_dma_pool_selftest_expect(SDRAM_DmaPoolPeak() == local_peak_before_reset,
                                   &pass, "reset keeps peak");
    sdram_dma_pool_selftest_expect(after_reset.zone_stats[XHGC_MEM_ZONE_DMA_POOL].peak >=
                                   after_128k.zone_stats[XHGC_MEM_ZONE_DMA_POOL].used,
                                   &pass, "meminfo peak kept");
    sdram_dma_pool_selftest_expect(after_reset.zone_stats[XHGC_MEM_ZONE_DMA_POOL].fail_count ==
                                   after_fail.zone_stats[XHGC_MEM_ZONE_DMA_POOL].fail_count,
                                   &pass, "reset keeps fail_count");

    printf("[XHGC DMA_POOL SELFTEST] %s\r\n", pass ? "PASS" : "FAIL");
    return pass;
}
#endif

/**
 * @brief  从 RESOURCE_ARENA 线性分配一块对齐内存
 * @param  size: 申请字节数
 * @param  align: 请求对齐字节数，小于 SDRAM_DEFAULT_ALIGN 时会提升到 SDRAM_DEFAULT_ALIGN
 * @return 非NULL=分配成功, NULL=参数非法、对齐非法或空间不足
 * @note   本函数仅移动 RESOURCE_ARENA 本地 offset，不更新 meminfo，不改变 RESOURCE_ARENA owner
 */
void *SDRAM_AppArenaAlloc(size_t size, size_t align)
{
    return sdram_linear_alloc(RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE,
                              &s_resource_arena_offset, size, align);
}

/**
 * @brief  复位 RESOURCE_ARENA 线性分配位置
 * @retval None
 * @note   布局边界检查失败会调用 Error_Handler；本函数不改变 RESOURCE_ARENA owner
 */
void SDRAM_AppArenaReset(void)
{
    if (RESOURCE_ARENA_BASE <= LUA_HEAP_END ||
        !sdram_range_tightly_follows(LUA_HEAP_END, RESOURCE_ARENA_BASE)) {
        Error_Handler();
        return;
    }

    s_resource_arena_offset = 0;
}

/**
 * @brief  获取 RESOURCE_ARENA 当前已用字节数
 * @retval 当前已用字节数
 */
size_t SDRAM_AppArenaUsed(void)
{
    return s_resource_arena_offset;
}

/**
 * @brief  获取 RESOURCE_ARENA 当前剩余字节数
 * @retval 当前剩余字节数
 */
size_t SDRAM_AppArenaFree(void)
{
    return RESOURCE_ARENA_SIZE - s_resource_arena_offset;
}
