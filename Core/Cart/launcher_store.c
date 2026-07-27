#include "launcher_store.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "crc.h"
#include "lfs_port.h"

#define LAUNCHER_INDEX_MAGIC      0x5844494Cu
#define LAUNCHER_INDEX_VERSION    1u
#define LAUNCHER_DIR_PATH         "/launcher"
#define LAUNCHER_INDEX_PATH       "/launcher/index.bin"
#define LAUNCHER_INDEX_TEMP_PATH  "/launcher/index.tmp"

typedef struct __attribute__((packed)) {
    uint8_t valid;
    uint8_t reserved[7];
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
} launcher_disk_app_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t generation;
    uint32_t crc32;
    uint32_t reserved[3];
    launcher_disk_app_t apps[LAUNCHER_STORE_MAX_APPS];
} launcher_disk_index_t;

static FLASH_Handle *s_flash;
static launcher_disk_index_t s_index;
static bool s_ready;
static const char *s_last_error = "Launcher store not initialized";

static void reset_index(void)
{
    memset(&s_index, 0, sizeof(s_index));
    s_index.magic = LAUNCHER_INDEX_MAGIC;
    s_index.version = LAUNCHER_INDEX_VERSION;
    s_index.total_size = sizeof(s_index);
}

static uint32_t index_crc(void)
{
    uint32_t saved_crc = s_index.crc32;
    s_index.crc32 = 0u;
    uint32_t crc = CRC32_IEEE_Calculate(&s_index, sizeof(s_index));
    s_index.crc32 = saved_crc;
    return crc;
}

static int restore_memory_mapped(void)
{
    if(s_flash == NULL) {
        return -1;
    }
    if(FLASH_EnableMemoryMapped(s_flash) != FLASH_OK) {
        s_last_error = "QFLASH Memory-Mapped restore failed";
        return -1;
    }
    return 0;
}

static int finish_operation(int result)
{
    if(restore_memory_mapped() != 0) {
        s_ready = false;
        return -20;
    }
    return result;
}

static int read_file_exact(const char *path, void *buffer, uint32_t size)
{
    lfs_file_t file;
    int result = lfs_file_open(&g_lfs, &file, path, LFS_O_RDONLY);
    if(result < 0) {
        return result;
    }

    lfs_soff_t file_size = lfs_file_size(&g_lfs, &file);
    if(file_size != (lfs_soff_t)size) {
        (void)lfs_file_close(&g_lfs, &file);
        return LFS_ERR_CORRUPT;
    }

    lfs_ssize_t read_size = lfs_file_read(&g_lfs, &file, buffer, size);
    int close_result = lfs_file_close(&g_lfs, &file);
    if(read_size != (lfs_ssize_t)size) {
        return read_size < 0 ? (int)read_size : LFS_ERR_IO;
    }
    return close_result;
}

static int write_file_atomic(const char *temp_path,
                             const char *final_path,
                             const void *data,
                             uint32_t size)
{
    lfs_file_t file;
    int result = lfs_file_open(&g_lfs,
                               &file,
                               temp_path,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if(result < 0) {
        return result;
    }

    lfs_ssize_t written = lfs_file_write(&g_lfs, &file, data, size);
    if(written == (lfs_ssize_t)size) {
        result = lfs_file_sync(&g_lfs, &file);
    } else {
        result = written < 0 ? (int)written : LFS_ERR_IO;
    }

    int close_result = lfs_file_close(&g_lfs, &file);
    if(result == 0 && close_result < 0) {
        result = close_result;
    }
    if(result < 0) {
        (void)lfs_remove(&g_lfs, temp_path);
        return result;
    }

    result = lfs_rename(&g_lfs, temp_path, final_path);
    if(result < 0) {
        (void)lfs_remove(&g_lfs, temp_path);
    }
    return result;
}

static void make_icon_path(char *path,
                           size_t path_size,
                           uint64_t cart_id,
                           uint32_t icon_crc,
                           bool temporary)
{
    uint32_t high = (uint32_t)(cart_id >> 32);
    uint32_t low = (uint32_t)cart_id;
    snprintf(path,
             path_size,
             temporary ? "/launcher/%08lX%08lX-%08lX.tmp"
                       : "/launcher/%08lX%08lX-%08lX.argb",
             (unsigned long)high,
             (unsigned long)low,
             (unsigned long)icon_crc);
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if(dst_size == 0u) {
        return;
    }
    if(src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static void make_disk_app(launcher_disk_app_t *app,
                          const CartBinInfo *info,
                          uint32_t icon_size,
                          uint32_t icon_crc)
{
    memset(app, 0, sizeof(*app));
    app->valid = 1u;
    app->cart_id = info->cart_id;
    app->file_size = info->file_size;
    app->icon_size = icon_size;
    app->icon_crc32 = icon_crc;
    copy_text(app->title, sizeof(app->title), info->title);
    copy_text(app->title_zh, sizeof(app->title_zh), info->title_zh);
    copy_text(app->publisher, sizeof(app->publisher), info->publisher);
    copy_text(app->version, sizeof(app->version), info->version);
    copy_text(app->entry, sizeof(app->entry), info->entry);
    copy_text(app->min_fw, sizeof(app->min_fw), info->min_fw);
}

static void copy_public_app(LauncherStoredApp *dst, const launcher_disk_app_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->valid = src->valid != 0u;
    dst->cart_id = src->cart_id;
    dst->file_size = src->file_size;
    dst->icon_size = src->icon_size;
    dst->icon_crc32 = src->icon_crc32;
    copy_text(dst->title, sizeof(dst->title), src->title);
    copy_text(dst->title_zh, sizeof(dst->title_zh), src->title_zh);
    copy_text(dst->publisher, sizeof(dst->publisher), src->publisher);
    copy_text(dst->version, sizeof(dst->version), src->version);
    copy_text(dst->entry, sizeof(dst->entry), src->entry);
    copy_text(dst->min_fw, sizeof(dst->min_fw), src->min_fw);
}

static int load_index(void)
{
    int result = read_file_exact(LAUNCHER_INDEX_PATH, &s_index, sizeof(s_index));
    if(result == LFS_ERR_NOENT) {
        reset_index();
        return 0;
    }
    if(result < 0
       || s_index.magic != LAUNCHER_INDEX_MAGIC
       || s_index.version != LAUNCHER_INDEX_VERSION
       || s_index.total_size != sizeof(s_index)
       || index_crc() != s_index.crc32) {
        reset_index();
        s_last_error = "Launcher index invalid; reset in memory";
        return -4;
    }
    return 0;
}

static int save_index(void)
{
    s_index.generation++;
    s_index.crc32 = 0u;
    s_index.crc32 = index_crc();
    return write_file_atomic(LAUNCHER_INDEX_TEMP_PATH,
                             LAUNCHER_INDEX_PATH,
                             &s_index,
                             sizeof(s_index));
}

int LauncherStore_Init(FLASH_Handle *flash)
{
    s_ready = false;
    s_flash = flash;
    reset_index();

    if(LFS_PortBind(flash) != 0) {
        s_last_error = "littlefs bind failed";
        (void)restore_memory_mapped();
        return -1;
    }

    int result = LFS_MountOrFormat();
    if(result < 0) {
        s_last_error = "littlefs mount failed";
        (void)restore_memory_mapped();
        return result;
    }

    result = lfs_mkdir(&g_lfs, LAUNCHER_DIR_PATH);
    if(result < 0 && result != LFS_ERR_EXIST) {
        s_last_error = "Launcher directory create failed";
        (void)restore_memory_mapped();
        return result;
    }

    result = load_index();
    s_ready = true;
    if(result == 0) {
        s_last_error = "OK";
    }
    return finish_operation(result);
}

bool LauncherStore_IsReady(void)
{
    return s_ready;
}

int LauncherStore_Get(uint8_t slot, LauncherStoredApp *out_app)
{
    if(!s_ready || out_app == NULL || slot >= LAUNCHER_STORE_MAX_APPS) {
        return -1;
    }
    if(s_index.apps[slot].valid == 0u) {
        return -2;
    }

    copy_public_app(out_app, &s_index.apps[slot]);
    return 0;
}

int LauncherStore_ReadIcon(uint8_t slot, void *buffer, uint32_t buffer_size)
{
    if(!s_ready || buffer == NULL || slot >= LAUNCHER_STORE_MAX_APPS) {
        return -1;
    }

    const launcher_disk_app_t *app = &s_index.apps[slot];
    if(app->valid == 0u || app->icon_size == 0u || buffer_size < app->icon_size) {
        return -2;
    }

    char path[56];
    make_icon_path(path, sizeof(path), app->cart_id, app->icon_crc32, false);
    int result = read_file_exact(path, buffer, app->icon_size);
    if(result == 0 && CRC32_IEEE_Calculate(buffer, app->icon_size) != app->icon_crc32) {
        result = -3;
        s_last_error = "Launcher icon CRC mismatch";
    }
    return finish_operation(result);
}

int LauncherStore_Upsert(const CartBinInfo *info,
                         const void *icon,
                         uint32_t icon_size,
                         uint8_t *out_slot)
{
    if(!s_ready || info == NULL || icon == NULL || icon_size == 0u) {
        return -1;
    }

    uint8_t slot = LAUNCHER_STORE_MAX_APPS;
    uint8_t empty_slot = LAUNCHER_STORE_MAX_APPS;
    for(uint8_t index = 0u; index < LAUNCHER_STORE_MAX_APPS; index++) {
        if(s_index.apps[index].valid != 0u && s_index.apps[index].cart_id == info->cart_id) {
            slot = index;
            break;
        }
        if(s_index.apps[index].valid == 0u && empty_slot == LAUNCHER_STORE_MAX_APPS) {
            empty_slot = index;
        }
    }
    if(slot == LAUNCHER_STORE_MAX_APPS) {
        slot = empty_slot;
    }
    if(slot == LAUNCHER_STORE_MAX_APPS) {
        s_last_error = "Launcher store is full";
        return -2;
    }

    uint32_t icon_crc = CRC32_IEEE_Calculate(icon, icon_size);
    launcher_disk_app_t next_app;
    make_disk_app(&next_app, info, icon_size, icon_crc);

    bool icon_changed = s_index.apps[slot].valid == 0u
                        || s_index.apps[slot].cart_id != info->cart_id
                        || s_index.apps[slot].icon_size != icon_size
                        || s_index.apps[slot].icon_crc32 != icon_crc;
    bool metadata_changed = memcmp(&s_index.apps[slot], &next_app, sizeof(next_app)) != 0;
    launcher_disk_app_t previous_app = s_index.apps[slot];

    int result = 0;
    if(icon_changed) {
        char temp_path[56];
        char final_path[56];
        make_icon_path(temp_path, sizeof(temp_path), info->cart_id, icon_crc, true);
        make_icon_path(final_path, sizeof(final_path), info->cart_id, icon_crc, false);
        result = write_file_atomic(temp_path, final_path, icon, icon_size);
    }

    if(result == 0 && metadata_changed) {
        uint32_t previous_generation = s_index.generation;
        uint32_t previous_crc = s_index.crc32;
        s_index.apps[slot] = next_app;
        result = save_index();
        if(result != 0) {
            s_index.apps[slot] = previous_app;
            s_index.generation = previous_generation;
            s_index.crc32 = previous_crc;
        }
    }
    if(result == 0
       && icon_changed
       && previous_app.valid != 0u
       && previous_app.cart_id == info->cart_id
       && previous_app.icon_crc32 != icon_crc) {
        char previous_path[56];
        make_icon_path(previous_path,
                       sizeof(previous_path),
                       previous_app.cart_id,
                       previous_app.icon_crc32,
                       false);
        (void)lfs_remove(&g_lfs, previous_path);
    }
    if(result == 0) {
        if(out_slot != NULL) {
            *out_slot = slot;
        }
        s_last_error = "OK";
    } else {
        s_last_error = "Launcher cache write failed";
    }
    return finish_operation(result);
}

const char *LauncherStore_LastError(void)
{
    return s_last_error;
}
