#include "sdram.h"

#include <stdio.h>

#include "main.h"

uint32_t timer;
uint32_t write_timer = 0, read_time = 0;

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