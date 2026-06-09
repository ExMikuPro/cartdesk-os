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

void SDRAM_Init(void);

/**
 * @brief Validate the fixed SDRAM partition layout and APP_ARENA_REST split.
 *
 * Call after SDRAM initialization succeeds and before SDRAM-backed allocators
 * are used. LUA_HEAP is reserved separately and resource arena reset cannot
 * reclaim it. The function enters Error_Handler() if any boundary or alignment
 * check fails.
 */
void sdram_layout_check(void);
void FMC_SDRAM_Write_Buffer(uint8_t *pBuffer, uint32_t WriteAddr, uint32_t n);
void FMC_SDRAM_Read_Buffer(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t n);
void FMC_SDRAM_Test(void);
void SDRAM_WriteSpeed_Test(void);
void SDRAM_ReadSpeed_Test(void);

int SDRAM_MinTest(void);

/*
 * DMA_POOL is a reset-only bump allocator for temporary DMA buffers.
 * Fixed DMA targets such as framebuffer zones, LAUNCHER_CACHE, and
 * APP_ARENA_REST resources are not allocated from this pool.
 */
void SDRAM_DmaPoolInit(void);
void SDRAM_DmaPoolReset(void);
void *SDRAM_DmaPoolAlloc(size_t size, size_t align);
void *SDRAM_DmaPoolCalloc(size_t count, size_t size, size_t align);
uint32_t SDRAM_DmaPoolUsed(void);
uint32_t SDRAM_DmaPoolPeak(void);
uint32_t SDRAM_DmaPoolFree(void);
bool SDRAM_DmaPoolContains(const void *ptr, size_t size);

#if XHGC_DMA_POOL_SELFTEST_ENABLE
bool SDRAM_DmaPoolSelftest(void);
#endif

/* Backward-compatible API names; these functions operate on RESOURCE_ARENA only. */
void *SDRAM_AppArenaAlloc(size_t size, size_t align);
void SDRAM_AppArenaReset(void);
size_t SDRAM_AppArenaUsed(void);
size_t SDRAM_AppArenaFree(void);

#endif
