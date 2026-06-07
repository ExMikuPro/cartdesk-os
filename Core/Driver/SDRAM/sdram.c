#include "sdram.h"

#include <stdio.h>

#include "main.h"

uint32_t timer;
uint32_t write_timer = 0, read_time = 0;

static size_t s_dma_pool_offset = 0;
static size_t s_app_arena_offset = 0;

static int sdram_range_tightly_follows(uintptr_t prev_end, uintptr_t next_base)
{
    return (prev_end + 1UL) == next_base;
}

static int sdram_range_aligned(uintptr_t base, uintptr_t end, size_t align)
{
    return ((base % align) == 0u) && (((end + 1UL) % align) == 0u);
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
        !sdram_range_aligned(RESOURCE_ARENA_BASE, RESOURCE_ARENA_END, SDRAM_DEFAULT_ALIGN) ||
        !sdram_range_aligned(COLD_POOL_BASE, COLD_POOL_END, SDRAM_DEFAULT_ALIGN)) {
        Error_Handler();
    }

    if (!sdram_addr_in_app_arena(RESOURCE_ARENA_BASE) ||
        !sdram_addr_in_app_arena(RESOURCE_ARENA_END) ||
        !sdram_addr_in_app_arena(COLD_POOL_BASE) ||
        !sdram_addr_in_app_arena(COLD_POOL_END) ||
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
    if (align < SDRAM_DMA_ALIGN) {
        align = SDRAM_DMA_ALIGN;
    }

    return sdram_linear_alloc(SDRAM_DMA_POOL_ADDR, SDRAM_DMA_POOL_SIZE, &s_dma_pool_offset, size, align);
}

void SDRAM_DmaPoolReset(void)
{
    s_dma_pool_offset = 0;
}

size_t SDRAM_DmaPoolUsed(void)
{
    return s_dma_pool_offset;
}

size_t SDRAM_DmaPoolFree(void)
{
    return SDRAM_DMA_POOL_SIZE - s_dma_pool_offset;
}

void *SDRAM_AppArenaAlloc(size_t size, size_t align)
{
    return sdram_linear_alloc(RESOURCE_ARENA_BASE, RESOURCE_ARENA_SIZE, &s_app_arena_offset, size, align);
}

void SDRAM_AppArenaReset(void)
{
    s_app_arena_offset = 0;
}

size_t SDRAM_AppArenaUsed(void)
{
    return s_app_arena_offset;
}

size_t SDRAM_AppArenaFree(void)
{
    return RESOURCE_ARENA_SIZE - s_app_arena_offset;
}
