#include "qflash_font_programmer.h"

#include <stdbool.h>
#include <string.h>

#include "flash.h"
#include "quadspi.h"
#include "sdram_layout.h"

#define QFLASH_TOTAL_SIZE        (64u * 1024u * 1024u)
#define QFLASH_FONT_REGION_SIZE  0x01000000u
#define VERIFY_BUFFER_SIZE       256u

static FLASH_Handle s_program_flash;
static bool s_program_session_active;

void __attribute__((noinline, used)) QFlashFont_ProgrammerReady(void)
{
    __NOP();
}

int QFlashFont_ProgramBegin(void)
{
    FLASH_Status status = FLASH_Open(&s_program_flash, &hqspi, QFLASH_TOTAL_SIZE);
    if(status == FLASH_OK) {
        status = FLASH_BringUp(&s_program_flash);
    }
    s_program_session_active = status == FLASH_OK;
    return (int)status;
}

uintptr_t QFlashFont_ProgramBufferAddress(void)
{
    return COLD_POOL_BASE;
}

static void invalidate_source_cache(const void *data, uint32_t length)
{
#if defined(__CORTEX_M) && (__CORTEX_M == 7)
    uintptr_t start = (uintptr_t)data & ~(uintptr_t)31u;
    uintptr_t end = ((uintptr_t)data + length + 31u) & ~(uintptr_t)31u;
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)data;
    (void)length;
#endif
}

static int verify_block(uint32_t offset, const uint8_t *expected, uint32_t length)
{
    uint8_t actual[VERIFY_BUFFER_SIZE];
    uint32_t position = 0u;

    while(position < length) {
        uint32_t chunk = length - position;
        if(chunk > sizeof(actual)) {
            chunk = sizeof(actual);
        }

        FLASH_Status status =
            FLASH_ReadFastQuad(&s_program_flash, offset + position, actual, chunk);
        if(status != FLASH_OK) {
            return (int)status;
        }
        if(memcmp(actual, expected + position, chunk) != 0) {
            return -4;
        }
        position += chunk;
    }
    return 0;
}

int QFlashFont_ProgramBlock(uint32_t offset, const void *data, uint32_t length)
{
    if(!s_program_session_active) {
        return -1;
    }
    if(data == NULL || length == 0u || length > QFLASH_FONT_PROGRAM_BLOCK_SIZE) {
        return -2;
    }
    if((offset % QFLASH_FONT_PROGRAM_BLOCK_SIZE) != 0u
       || offset >= QFLASH_FONT_REGION_SIZE
       || length > QFLASH_FONT_REGION_SIZE - offset) {
        return -3;
    }

    invalidate_source_cache(data, length);

    FLASH_Status status = FLASH_Erase64K(&s_program_flash, offset);
    if(status != FLASH_OK) {
        return (int)status;
    }
    status = FLASH_Prog(&s_program_flash, offset, data, length);
    if(status != FLASH_OK) {
        return (int)status;
    }
    return verify_block(offset, data, length);
}

int QFlashFont_ProgramFinish(void)
{
    if(!s_program_session_active) {
        return -1;
    }

    FLASH_Status status = FLASH_EnableMemoryMapped(&s_program_flash);
    s_program_session_active = false;
    return (int)status;
}
