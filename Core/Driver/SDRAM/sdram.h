#ifndef __SDRAM_H
#define __SDRAM_H

#include "../../Inc/main.h"
#include "../../Inc/sdram_layout.h"
#include <stddef.h>

extern SDRAM_HandleTypeDef hsdram1;

#define FMC_SDRAM_ADDR   ((uint32_t)(SDRAM_BASE_ADDR)) /* SDRAM base address */

#define EXT_SDRAM_SIZE	 (64*1024*1024)


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

void SDRAM_Init(void);
void FMC_SDRAM_Write_Buffer(uint8_t *pBuffer, uint32_t WriteAddr, uint32_t n);
void FMC_SDRAM_Read_Buffer(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t n);
void FMC_SDRAM_Test(void);
void SDRAM_WriteSpeed_Test(void);
void SDRAM_ReadSpeed_Test(void);

int SDRAM_MinTest(void);

void *SDRAM_DmaPoolAlloc(size_t size, size_t align);
void SDRAM_DmaPoolReset(void);
size_t SDRAM_DmaPoolUsed(void);
size_t SDRAM_DmaPoolFree(void);

void *SDRAM_AppArenaAlloc(size_t size, size_t align);
void SDRAM_AppArenaReset(void);
size_t SDRAM_AppArenaUsed(void);
size_t SDRAM_AppArenaFree(void);

#endif
