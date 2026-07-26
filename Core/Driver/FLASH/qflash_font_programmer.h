#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dual-flash mode erases one 64 KiB block on each chip: 128 KiB logical. */
#define QFLASH_FONT_PROGRAM_BLOCK_SIZE  0x00020000u

/**
 * @brief Stable breakpoint reached after SDRAM and QUADSPI initialization.
 */
void QFlashFont_ProgrammerReady(void);

/**
 * @brief Initialize a GDB-driven QFLASH programming session.
 * @return 0 on success, otherwise a positive FLASH_Status value.
 */
int QFlashFont_ProgramBegin(void);

/**
 * @brief Return the SDRAM staging address reserved while halted at the ready breakpoint.
 */
uintptr_t QFlashFont_ProgramBufferAddress(void);

/**
 * @brief Erase, program, and verify one block in the 16 MiB font partition.
 * @param offset QFLASH byte offset, aligned to 128 KiB.
 * @param data Source buffer in SDRAM.
 * @param length Number of valid bytes, 1..128 KiB.
 * @return 0 on success, negative value for validation/verify errors, or FLASH_Status.
 */
int QFlashFont_ProgramBlock(uint32_t offset, const void *data, uint32_t length);

/**
 * @brief Finish the session and restore QSPI Memory-Mapped mode.
 */
int QFlashFont_ProgramFinish(void);

#ifdef __cplusplus
}
#endif
