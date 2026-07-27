#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cart_bin.h"
#include "flash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_STORE_MAX_APPS 12u

typedef struct {
    bool valid;
    uint64_t cart_id;
    uint64_t file_size;
    uint32_t icon_size;
    uint32_t icon_crc32;
    char title[CART_BIN_TITLE_SIZE + 1u];
    char title_zh[CART_BIN_TITLE_ZH_SIZE + 1u];
    char publisher[CART_BIN_PUBLISHER_SIZE + 1u];
    char version[CART_BIN_VERSION_SIZE + 1u];
    char entry[CART_BIN_ENTRY_SIZE + 1u];
    char min_fw[CART_BIN_MIN_FW_SIZE + 1u];
} LauncherStoredApp;

/**
 * @brief Mount the QFLASH littlefs partition and load the Launcher index.
 *
 * The supplied Flash handle is shared with the QFLASH font provider. Every
 * storage transaction restores Memory-Mapped mode before returning.
 */
int LauncherStore_Init(FLASH_Handle *flash);

bool LauncherStore_IsReady(void);

/**
 * @brief Copy one persistent Launcher record.
 * @return 0 on success, negative when the slot is invalid or unused.
 */
int LauncherStore_Get(uint8_t slot, LauncherStoredApp *out_app);

/**
 * @brief Read and CRC-check one cached ARGB icon.
 */
int LauncherStore_ReadIcon(uint8_t slot, void *buffer, uint32_t buffer_size);

/**
 * @brief Insert or update a card record and its icon.
 *
 * Existing cart_id values retain their slot. New cards use the first empty
 * slot. The icon and index are committed through temporary files.
 */
int LauncherStore_Upsert(const CartBinInfo *info,
                         const void *icon,
                         uint32_t icon_size,
                         uint8_t *out_slot);

const char *LauncherStore_LastError(void);

#ifdef __cplusplus
}
#endif
