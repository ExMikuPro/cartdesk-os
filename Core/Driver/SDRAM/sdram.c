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

static void sdram_dma_pool_record_fail(size_t size)
{
    (void)xhgc_meminfo_fail_record(XHGC_MEM_ZONE_DMA_POOL,
                                   size > UINT32_MAX ? UINT32_MAX : (uint32_t)size,
                                   XHGC_MEM_TAG_DMA);
}

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
 * @brief       SDRAM��ʼ��
 * @param       ��
 * @retval      ��
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
 * @brief  Write buffer to SDRAM
 * @param  pBuffer: Pointer to data buffer
 * @param  WriteAddr: Write address offset
 * @param  n: Number of bytes to write
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
 * @brief  Read buffer from SDRAM
 * @param  pBuffer: Pointer to data buffer
 * @param  ReadAddr: Read address offset
 * @param  n: Number of bytes to read
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
 * @brief  SDRAM memory test
 * @param  None
 * @retval None
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
 * @brief  SDRAM write speed test
 * @param  None
 * @retval None
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
 * @brief  SDRAM read speed test
 * @param  None
 * @retval None
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

void *SDRAM_DmaPoolAlloc(size_t size, size_t align)
{
    return sdram_dma_pool_alloc_internal(size, align);
}

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

void SDRAM_DmaPoolInit(void)
{
    s_dma_pool_offset = 0u;
    s_dma_pool_peak = 0u;
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_DMA_POOL, XHGC_MEM_TAG_DMA);
}

void SDRAM_DmaPoolReset(void)
{
    s_dma_pool_offset = 0u;
    (void)xhgc_meminfo_zone_reset_tag(XHGC_MEM_ZONE_DMA_POOL, XHGC_MEM_TAG_DMA);
}

uint32_t SDRAM_DmaPoolUsed(void)
{
    return s_dma_pool_offset > UINT32_MAX ? UINT32_MAX : (uint32_t)s_dma_pool_offset;
}

uint32_t SDRAM_DmaPoolPeak(void)
{
    return s_dma_pool_peak > UINT32_MAX ? UINT32_MAX : (uint32_t)s_dma_pool_peak;
}

uint32_t SDRAM_DmaPoolFree(void)
{
    const XHGC_MemZoneDesc *zone = sdram_dma_pool_zone();

    if (zone == NULL || s_dma_pool_offset >= zone->size) {
        return 0u;
    }

    return (uint32_t)(zone->size - s_dma_pool_offset);
}

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

void *SDRAM_AppArenaAlloc(size_t size, size_t align)
{
    return sdram_linear_alloc(RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE,
                              &s_resource_arena_offset, size, align);
}

void SDRAM_AppArenaReset(void)
{
    if (RESOURCE_ARENA_BASE <= LUA_HEAP_END ||
        !sdram_range_tightly_follows(LUA_HEAP_END, RESOURCE_ARENA_BASE)) {
        Error_Handler();
        return;
    }

    s_resource_arena_offset = 0;
}

size_t SDRAM_AppArenaUsed(void)
{
    return s_resource_arena_offset;
}

size_t SDRAM_AppArenaFree(void)
{
    return RESOURCE_ARENA_SIZE - s_resource_arena_offset;
}
