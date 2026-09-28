#include "cart_io_service.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "stm32h743xx.h"

#include "cart_bin.h"
#include "cart_storage_format.h"
#include "crash_record.h"
#include "crc.h"
#include "flash.h"
#include "launcher_store.h"
#include "lfs.h"
#include "lfs_port.h"
#include "quadspi.h"
#include "fatfs.h"
#include "xhgc_cart.h"

#define CART_IO_READY_FLAG 0x00000001u
#define CART_IO_CANCELLED_OWNER_CAPACITY 32u
#define CART_IO_QFLASH_SIZE (64u * 1024u * 1024u)
#define CART_IO_RESOURCE_INDEX_MAX (64u * 1024u)
#define CART_IO_RESOURCE_INDEX_ENVELOPE_SIZE 12u
static osMessageQueueId_t s_request_queue;
static osMessageQueueId_t s_completion_queue;
static osEventFlagsId_t s_state_flags;
static FLASH_Handle s_qflash;
static uint32_t s_next_request_id = 1u;
static uint32_t s_cancelled_owners[CART_IO_CANCELLED_OWNER_CAPACITY];
static uint32_t s_cancelled_owner_cursor;
static cart_task_stats_t s_stats;
static volatile uint32_t s_qflash_pending;
static volatile uint32_t s_sd_pending;
static volatile bool s_sd_exclusive;
static XHGC_CartFatFs s_resource_cart;
static XHGC_CartSlot s_resource_data_slot;
static uint32_t s_resource_session_id;
static uint32_t s_resource_generation;
static uint32_t s_resource_owner_id;
static bool s_resource_session_open;

static bool operation_uses_qflash(cart_io_operation_t operation)
{
    return operation == CART_IO_OP_LAUNCHER_STORE_READ_ICON ||
           operation == CART_IO_OP_LAUNCHER_STORE_UPSERT ||
           operation == CART_IO_OP_STORAGE_LOAD ||
           operation == CART_IO_OP_STORAGE_COMMIT ||
           operation == CART_IO_OP_STORAGE_CLEAR;
}

static bool operation_uses_sd(cart_io_operation_t operation)
{
    return operation == CART_IO_OP_CRASH_LOG_APPEND ||
           operation == CART_IO_OP_CART_PROBE ||
           operation == CART_IO_OP_CART_READ_INFO ||
           operation == CART_IO_OP_CART_READ_RESOURCE ||
           operation == CART_IO_OP_RESOURCE_SESSION_OPEN ||
           operation == CART_IO_OP_RESOURCE_INDEX_READ ||
           operation == CART_IO_OP_RESOURCE_BLOB_READ ||
           operation == CART_IO_OP_RESOURCE_SESSION_CLOSE;
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *p, uint64_t value)
{
    write_le32(p, (uint32_t)value);
    write_le32(p + 4u, (uint32_t)(value >> 32));
}

static void resource_session_close(void)
{
    if (s_resource_session_open) xhgc_cart_close_fatfs(&s_resource_cart);
    memset(&s_resource_cart, 0, sizeof(s_resource_cart));
    memset(&s_resource_data_slot, 0, sizeof(s_resource_data_slot));
    s_resource_session_id = 0u;
    s_resource_generation = 0u;
    s_resource_owner_id = 0u;
    s_resource_session_open = false;
}

static bool resource_session_matches(uint32_t session_id, uint32_t generation)
{
    return s_resource_session_open && session_id != 0u &&
           s_resource_session_id == session_id &&
           s_resource_generation == generation;
}

static cart_io_status_t resource_session_open_request(
    const cart_io_request_t *request)
{
    const char *path = request->params.resource_open.path;
    XHGC_CartSlot data_slot;
    if (path[0] == '\0' ||
        request->params.resource_open.session_id == 0u ||
        request->params.resource_open.generation == 0u) {
        return CART_IO_STATUS_INVALID_ARGUMENT;
    }
    resource_session_close();
    if (SD_FATFS_Mount() != FR_OK ||
        xhgc_cart_open_fatfs(&s_resource_cart, path) != XHGC_CART_OK ||
        xhgc_cart_get_slot(&s_resource_cart.cart, XHGC_CART_SLOT_DATA,
                           &data_slot) != XHGC_CART_OK) {
        resource_session_close();
        return CART_IO_STATUS_IO_ERROR;
    }
    s_resource_data_slot = data_slot;
    s_resource_session_id = request->params.resource_open.session_id;
    s_resource_generation = request->params.resource_open.generation;
    s_resource_owner_id = request->owner_id;
    s_resource_session_open = true;
    return CART_IO_STATUS_OK;
}

static cart_io_status_t resource_index_read_request(
    const cart_io_request_t *request, cart_io_completion_t *completion)
{
    XHGC_CartSlot index_slot;
    uint32_t allocation_size;
    uint8_t *bytes;
    if (!resource_session_matches(request->params.resource_session.session_id,
                                  request->params.resource_session.generation)) {
        return CART_IO_STATUS_CANCELLED;
    }
    if (xhgc_cart_get_slot(&s_resource_cart.cart, XHGC_CART_SLOT_INDEX,
                           &index_slot) != XHGC_CART_OK ||
        index_slot.size == 0u || index_slot.size > CART_IO_RESOURCE_INDEX_MAX ||
        UINT32_MAX - CART_IO_RESOURCE_INDEX_ENVELOPE_SIZE < index_slot.size) {
        return CART_IO_STATUS_CORRUPT;
    }
    allocation_size = CART_IO_RESOURCE_INDEX_ENVELOPE_SIZE + index_slot.size;
    bytes = (uint8_t *)pvPortMalloc(allocation_size);
    if (!bytes) return CART_IO_STATUS_NO_MEMORY;
    write_le64(bytes, s_resource_data_slot.offset);
    write_le32(bytes + 8u, s_resource_data_slot.size);
    if (s_resource_cart.cart.read(s_resource_cart.cart.reader_ctx,
                                  index_slot.offset,
                                  bytes + CART_IO_RESOURCE_INDEX_ENVELOPE_SIZE,
                                  index_slot.size) != 0) {
        vPortFree(bytes);
        return CART_IO_STATUS_IO_ERROR;
    }
    if (CRC32_IEEE_Calculate(bytes + CART_IO_RESOURCE_INDEX_ENVELOPE_SIZE,
                             index_slot.size) != index_slot.crc32) {
        vPortFree(bytes);
        return CART_IO_STATUS_CORRUPT;
    }
    completion->result.buffer = (cart_task_buffer_t) {
        .data = bytes,
        .capacity = allocation_size,
        .length = allocation_size,
        .owner_id = request->owner_id,
        .source = CART_BUFFER_SOURCE_RTOS_HEAP,
    };
    return CART_IO_STATUS_OK;
}

static cart_io_status_t resource_blob_read_request(
    const cart_io_request_t *request, cart_io_completion_t *completion)
{
    const cart_task_buffer_t *output = &request->params.resource_read.output;
    uint64_t end = (uint64_t)request->params.resource_read.data_offset +
                   request->params.resource_read.data_size;
    uint64_t absolute = s_resource_data_slot.offset +
                        request->params.resource_read.data_offset;
    completion->result.buffer = *output;
    completion->result.buffer.length = 0u;
    if (!resource_session_matches(request->params.resource_read.session_id,
                                  request->params.resource_read.generation)) {
        return CART_IO_STATUS_CANCELLED;
    }
    if (!output->data || output->capacity < request->params.resource_read.data_size ||
        request->params.resource_read.data_size == 0u ||
        end > s_resource_data_slot.size || absolute < s_resource_data_slot.offset) {
        return CART_IO_STATUS_INVALID_ARGUMENT;
    }
    if (s_resource_cart.cart.read(s_resource_cart.cart.reader_ctx, absolute,
                                  output->data,
                                  request->params.resource_read.data_size) != 0) {
        return CART_IO_STATUS_IO_ERROR;
    }
    if (request->params.resource_read.expected_crc32 != 0u &&
        CRC32_IEEE_Calculate(output->data,
                            request->params.resource_read.data_size) !=
            request->params.resource_read.expected_crc32) {
        return CART_IO_STATUS_CORRUPT;
    }
    completion->result.buffer.length = request->params.resource_read.data_size;
    return CART_IO_STATUS_OK;
}

static void storage_paths(uint64_t cart_id, char *dir, char *path, char *temp)
{
    (void)snprintf(dir, 40u, "/apps/%08lX%08lX",
                   (unsigned long)(cart_id >> 32), (unsigned long)cart_id);
    (void)snprintf(path, 64u, "%s/storage.bin", dir);
    (void)snprintf(temp, 64u, "%s/storage.tmp", dir);
}

static cart_io_status_t storage_load(const cart_io_request_t *request,
                                     cart_io_completion_t *completion)
{
    cart_task_buffer_t buffer = request->params.storage.payload;
    completion->result.storage.buffer = buffer;
    completion->result.storage.buffer.length = 0u;
    if(buffer.data == NULL || buffer.capacity == 0u ||
       request->params.storage.cart_id == 0u) return CART_IO_STATUS_INVALID_ARGUMENT;

    char dir[40], path[64], temp[64];
    storage_paths(request->params.storage.cart_id, dir, path, temp);
    if(LFS_EnableMappedRead(0) != 0) return CART_IO_STATUS_IO_ERROR;
    lfs_file_t file;
    int rc = lfs_file_open(&g_lfs, &file, path, LFS_O_RDONLY);
    if(rc == LFS_ERR_NOENT) {
        int temp_rc = lfs_file_open(&g_lfs, &file, temp, LFS_O_RDONLY);
        if(temp_rc >= 0) {
            (void)lfs_file_close(&g_lfs, &file);
            if(lfs_rename(&g_lfs, temp, path) >= 0)
                rc = lfs_file_open(&g_lfs, &file, path, LFS_O_RDONLY);
            else
                rc = LFS_ERR_CORRUPT;
        } else {
            (void)LFS_EnableMappedRead(1);
            return CART_IO_STATUS_NOT_FOUND;
        }
    }
    uint8_t header[CART_STORAGE_HEADER_SIZE];
    cart_storage_metadata_t metadata;
    if(rc >= 0 && lfs_file_read(&g_lfs, &file, header, sizeof(header)) !=
                    (lfs_ssize_t)sizeof(header)) rc = LFS_ERR_CORRUPT;
    if(rc >= 0 && !CartStorageFormat_DecodeHeader(header, buffer.capacity,
                                                   &metadata))
        rc = LFS_ERR_CORRUPT;
    if(rc >= 0 && lfs_file_read(&g_lfs, &file, buffer.data,
                                metadata.payload_size) !=
                    (lfs_ssize_t)metadata.payload_size) rc = LFS_ERR_CORRUPT;
    if(rc >= 0 && lfs_file_size(&g_lfs, &file) !=
                    (lfs_soff_t)(sizeof(header) + metadata.payload_size))
        rc = LFS_ERR_CORRUPT;
    if(rc >= 0 && !CartStorageFormat_VerifyPayload(
                       &metadata, buffer.data, metadata.payload_size))
        rc = LFS_ERR_CORRUPT;
    if(rc >= 0) {
        completion->result.storage.buffer.length = metadata.payload_size;
        completion->result.storage.entry_count = metadata.entry_count;
    }
    (void)lfs_file_close(&g_lfs, &file);
    (void)LFS_EnableMappedRead(1);
    return rc >= 0 ? CART_IO_STATUS_OK
                   : (rc == LFS_ERR_CORRUPT ? CART_IO_STATUS_CORRUPT
                                            : CART_IO_STATUS_IO_ERROR);
}

static cart_io_status_t storage_commit(const cart_io_request_t *request,
                                       cart_io_completion_t *completion)
{
    cart_task_buffer_t buffer = request->params.storage.payload;
    completion->result.storage.buffer = buffer;
    if(buffer.data == NULL || buffer.length > buffer.capacity ||
       request->params.storage.cart_id == 0u) return CART_IO_STATUS_INVALID_ARGUMENT;
    char dir[40], path[64], temp[64];
    storage_paths(request->params.storage.cart_id, dir, path, temp);
    if(LFS_EnableMappedRead(0) != 0) return CART_IO_STATUS_IO_ERROR;
    int rc = lfs_mkdir(&g_lfs, "/apps");
    if(rc == LFS_ERR_EXIST) rc = 0;
    if(rc >= 0) {
        rc = lfs_mkdir(&g_lfs, dir);
        if(rc == LFS_ERR_EXIST) rc = 0;
    }
    lfs_file_t file;
    bool opened = false;
    if(rc >= 0) {
        rc = lfs_file_open(&g_lfs, &file, temp,
                           LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        opened = rc >= 0;
    }
    uint8_t header[CART_STORAGE_HEADER_SIZE];
    CartStorageFormat_EncodeHeader(header, request->params.storage.entry_count,
                                   buffer.data, buffer.length);
    if(rc >= 0 && lfs_file_write(&g_lfs, &file, &header, sizeof(header)) !=
                    (lfs_ssize_t)sizeof(header)) rc = LFS_ERR_IO;
    if(rc >= 0 && lfs_file_write(&g_lfs, &file, buffer.data, buffer.length) !=
                    (lfs_ssize_t)buffer.length) rc = LFS_ERR_IO;
    if(rc >= 0) rc = lfs_file_sync(&g_lfs, &file);
    if(opened) {
        int close_rc = lfs_file_close(&g_lfs, &file);
        if(rc >= 0) rc = close_rc;
    }
    if(rc >= 0) rc = lfs_rename(&g_lfs, temp, path);
    if(rc < 0) (void)lfs_remove(&g_lfs, temp);
    (void)LFS_EnableMappedRead(1);
    return rc >= 0 ? CART_IO_STATUS_OK : CART_IO_STATUS_IO_ERROR;
}

static bool owner_is_cancelled(uint32_t owner_id)
{
    if (owner_id == 0u) {
        return false;
    }
    for (size_t i = 0u; i < CART_IO_CANCELLED_OWNER_CAPACITY; ++i) {
        if (s_cancelled_owners[i] == owner_id) {
            return true;
        }
    }
    return false;
}

static cart_io_status_t map_result(int result)
{
    if (result == 0) {
        return CART_IO_STATUS_OK;
    }
    if (result == -2) {
        return CART_IO_STATUS_NOT_FOUND;
    }
    if (result == -3 || result == -4) {
        return CART_IO_STATUS_CORRUPT;
    }
    return CART_IO_STATUS_IO_ERROR;
}

static cart_io_status_t process_request(const cart_io_request_t *request,
                                        cart_io_completion_t *completion)
{
    int result;

    switch (request->operation) {
        case CART_IO_OP_CRASH_LOG_APPEND:
            return CrashRecord_FlushPendingToSd() ? CART_IO_STATUS_OK
                                                  : CART_IO_STATUS_IO_ERROR;

        case CART_IO_OP_CART_PROBE:
        case CART_IO_OP_CART_READ_INFO:
            if (request->params.cart.output.data == NULL ||
                request->params.cart.output.capacity < sizeof(CartBinInfo) ||
                request->params.cart.path[0] == '\0') {
                return CART_IO_STATUS_INVALID_ARGUMENT;
            }
            result = cart_bin_read_info_from_sd(
                request->params.cart.path,
                (CartBinInfo *)request->params.cart.output.data);
            completion->result.buffer = request->params.cart.output;
            completion->result.buffer.length = result == 0 ? sizeof(CartBinInfo) : 0u;
            return map_result(result);

        case CART_IO_OP_CART_READ_RESOURCE:
            if (request->params.cart.output.data == NULL ||
                request->params.cart.output.capacity < CART_BIN_PREVIEW_SIZE ||
                request->params.cart.path[0] == '\0') {
                return CART_IO_STATUS_INVALID_ARGUMENT;
            }
            result = cart_bin_read_preview_from_sd(
                request->params.cart.path,
                (uint8_t *)request->params.cart.output.data,
                request->params.cart.output.capacity);
            completion->result.buffer = request->params.cart.output;
            completion->result.buffer.length = result == 0 ? CART_BIN_PREVIEW_SIZE : 0u;
            return map_result(result);

        case CART_IO_OP_LAUNCHER_STORE_READ_ICON:
            if (request->params.launcher_read_icon.output.data == NULL) {
                return CART_IO_STATUS_INVALID_ARGUMENT;
            }
            result = LauncherStore_ReadIcon(
                request->params.launcher_read_icon.slot,
                request->params.launcher_read_icon.output.data,
                request->params.launcher_read_icon.output.capacity);
            completion->result.buffer = request->params.launcher_read_icon.output;
            completion->result.buffer.length = result == 0 ? CART_BIN_PREVIEW_SIZE : 0u;
            return map_result(result);

        case CART_IO_OP_LAUNCHER_STORE_UPSERT: {
            const cart_task_buffer_t *info = &request->params.launcher_upsert.info;
            const cart_task_buffer_t *icon = &request->params.launcher_upsert.icon;
            if (info->data == NULL || info->length < sizeof(CartBinInfo) ||
                icon->data == NULL || icon->length == 0u) {
                return CART_IO_STATUS_INVALID_ARGUMENT;
            }
            uint8_t slot = 0u;
            result = LauncherStore_Upsert((const CartBinInfo *)info->data,
                                          icon->data, icon->length, &slot);
            completion->result.launcher_slot = slot;
            return map_result(result);
        }

        case CART_IO_OP_STORAGE_LOAD:
            return storage_load(request, completion);

        case CART_IO_OP_STORAGE_COMMIT:
            return storage_commit(request, completion);

        case CART_IO_OP_STORAGE_CLEAR: {
            char dir[40], path[64], temp[64];
            storage_paths(request->params.storage.cart_id, dir, path, temp);
            if(LFS_EnableMappedRead(0) != 0) return CART_IO_STATUS_IO_ERROR;
            result = lfs_remove(&g_lfs, path);
            if(result == LFS_ERR_NOENT) result = 0;
            (void)lfs_remove(&g_lfs, temp);
            (void)LFS_EnableMappedRead(1);
            return result == 0 ? CART_IO_STATUS_OK : CART_IO_STATUS_IO_ERROR;
        }

        case CART_IO_OP_RESOURCE_SESSION_OPEN:
            return resource_session_open_request(request);

        case CART_IO_OP_RESOURCE_INDEX_READ:
            return resource_index_read_request(request, completion);

        case CART_IO_OP_RESOURCE_BLOB_READ:
            return resource_blob_read_request(request, completion);

        case CART_IO_OP_RESOURCE_SESSION_CLOSE:
            if (resource_session_matches(
                    request->params.resource_session.session_id,
                    request->params.resource_session.generation)) {
                resource_session_close();
            }
            return CART_IO_STATUS_OK;

        case CART_IO_OP_INITIALIZE:
        case CART_IO_OP_LAUNCHER_STORE_LOAD:
        case CART_IO_OP_NONE:
        default:
            return CART_IO_STATUS_NOT_READY;
    }
}

bool CartIoService_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_cancelled_owners, 0, sizeof(s_cancelled_owners));
    s_cancelled_owner_cursor = 0u;
    s_qflash_pending = 0u;
    s_sd_pending = 0u;
    s_sd_exclusive = false;
    resource_session_close();
    s_request_queue = osMessageQueueNew(CART_IO_REQUEST_QUEUE_DEPTH,
                                        sizeof(cart_io_request_t), NULL);
    s_completion_queue = osMessageQueueNew(CART_IO_COMPLETION_QUEUE_DEPTH,
                                           sizeof(cart_io_completion_t), NULL);
    s_state_flags = osEventFlagsNew(NULL);
    return s_request_queue != NULL && s_completion_queue != NULL &&
           s_state_flags != NULL;
}

bool CartIoService_WorkerInitialize(void)
{
    FLASH_Status status = FLASH_Open(&s_qflash, &hqspi, CART_IO_QFLASH_SIZE);
    if (status == FLASH_OK) {
        status = FLASH_BringUp(&s_qflash);
    }
    if (status == FLASH_OK) {
        status = FLASH_EnableMemoryMapped(&s_qflash);
    }
    if (status == FLASH_OK) {
        (void)LauncherStore_Init(&s_qflash);
        if (s_qflash.memory_mapped || FLASH_EnableMemoryMapped(&s_qflash) == FLASH_OK) {
            (void)osEventFlagsSet(s_state_flags, CART_IO_READY_FLAG);
            return true;
        }
    }
    return false;
}

uint32_t CartIoService_NextRequestId(void)
{
    taskENTER_CRITICAL();
    uint32_t request_id = s_next_request_id++;
    if (s_next_request_id == 0u) {
        s_next_request_id = 1u;
    }
    taskEXIT_CRITICAL();
    return request_id;
}

bool CartIoService_Submit(const cart_io_request_t *request, uint32_t timeout_ms)
{
    bool uses_sd;

    if (request == NULL || request->request_id == 0u ||
        request->operation == CART_IO_OP_NONE || owner_is_cancelled(request->owner_id)) {
        return false;
    }
    uses_sd = operation_uses_sd(request->operation);
    if (uses_sd) {
        taskENTER_CRITICAL();
        if (s_sd_exclusive) {
            taskEXIT_CRITICAL();
            return false;
        }
        ++s_sd_pending;
        taskEXIT_CRITICAL();
    }
    cart_io_request_t queued = *request;
    queued.timeout_ms = timeout_ms;
    queued.submitted_tick = osKernelGetTickCount();
    if (osMessageQueuePut(s_request_queue, &queued, 0u, 0u) != osOK) {
        if (uses_sd) {
            taskENTER_CRITICAL();
            if (s_sd_pending > 0u) --s_sd_pending;
            taskEXIT_CRITICAL();
        }
        ++s_stats.queue_full;
        return false;
    }
    if (operation_uses_qflash(queued.operation)) {
        taskENTER_CRITICAL();
        ++s_qflash_pending;
        taskEXIT_CRITICAL();
    }
    uint32_t depth = osMessageQueueGetCount(s_request_queue);
    if (depth > s_stats.max_queue_depth) {
        s_stats.max_queue_depth = depth;
    }
    return true;
}

bool CartIoService_TryReceive(cart_io_completion_t *completion)
{
    return completion != NULL &&
           osMessageQueueGet(s_completion_queue, completion, NULL, 0u) == osOK;
}

bool CartIoService_CancelOwner(uint32_t owner_id)
{
    if (owner_id == 0u) {
        return false;
    }
    for (size_t i = 0u; i < CART_IO_CANCELLED_OWNER_CAPACITY; ++i) {
        if (s_cancelled_owners[i] == owner_id) {
            return true;
        }
        if (s_cancelled_owners[i] == 0u) {
            s_cancelled_owners[i] = owner_id;
            return true;
        }
    }
    s_cancelled_owners[s_cancelled_owner_cursor] = owner_id;
    s_cancelled_owner_cursor =
        (s_cancelled_owner_cursor + 1u) % CART_IO_CANCELLED_OWNER_CAPACITY;
    return true;
}

bool CartIoService_IsReady(void)
{
    return s_state_flags != NULL &&
           (osEventFlagsGet(s_state_flags) & CART_IO_READY_FLAG) != 0u;
}

bool CartIoService_WaitReady(uint32_t timeout_ms)
{
    uint32_t flags = osEventFlagsWait(s_state_flags, CART_IO_READY_FLAG,
                                      osFlagsWaitAny | osFlagsNoClear, timeout_ms);
    return (flags & osFlagsError) == 0u && (flags & CART_IO_READY_FLAG) != 0u;
}

bool CartIoService_IsQflashExclusive(void)
{
    return s_qflash_pending != 0u;
}

bool CartIoService_BeginSdExclusive(void)
{
    bool acquired = false;

    taskENTER_CRITICAL();
    if (!s_sd_exclusive && s_sd_pending == 0u) {
        s_sd_exclusive = true;
        acquired = true;
    }
    taskEXIT_CRITICAL();
    return acquired;
}

void CartIoService_EndSdExclusive(void)
{
    taskENTER_CRITICAL();
    s_sd_exclusive = false;
    taskEXIT_CRITICAL();
}

bool CartIoService_IsSdExclusive(void)
{
    return s_sd_exclusive;
}

void CartIoService_GetStats(cart_task_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}

void CartTaskBuffer_Release(cart_task_buffer_t *buffer)
{
    if(buffer == NULL || buffer->data == NULL) return;
    if(buffer->source == CART_BUFFER_SOURCE_RTOS_HEAP) {
        vPortFree(buffer->data);
    }
    memset(buffer, 0, sizeof(*buffer));
}

void CartIoService_WorkerRun(void)
{
    cart_io_request_t request;
    for (;;) {
        if (osMessageQueueGet(s_request_queue, &request, NULL, osWaitForever) != osOK) {
            continue;
        }

        cart_io_completion_t completion = {
            .request_id = request.request_id,
            .owner_id = request.owner_id,
            .operation = request.operation,
            .status = CART_IO_STATUS_IO_ERROR,
        };
        if (request.operation == CART_IO_OP_STORAGE_LOAD ||
            request.operation == CART_IO_OP_STORAGE_COMMIT ||
            request.operation == CART_IO_OP_STORAGE_CLEAR) {
            completion.result.storage.buffer = request.params.storage.payload;
        } else if (request.operation == CART_IO_OP_RESOURCE_BLOB_READ) {
            completion.result.buffer = request.params.resource_read.output;
        }
        uint32_t start_cycles = DWT->CYCCNT;
        uint32_t now = osKernelGetTickCount();
        if (owner_is_cancelled(request.owner_id) &&
            request.operation != CART_IO_OP_RESOURCE_SESSION_CLOSE) {
            completion.status = CART_IO_STATUS_CANCELLED;
        } else if (request.timeout_ms != 0u &&
                   (uint32_t)(now - request.submitted_tick) >= request.timeout_ms) {
            completion.status = CART_IO_STATUS_TIMEOUT;
            ++s_stats.timeout;
        } else {
            completion.status = process_request(&request, &completion);
        }

        ++s_stats.heartbeat;
        s_stats.last_heartbeat_tick = osKernelGetTickCount();
        ++s_stats.processed;
        if (completion.status != CART_IO_STATUS_OK) {
            ++s_stats.failed;
        }
        s_stats.stack_high_water = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u && SystemCoreClock >= 1000000u) {
            uint32_t duration = (DWT->CYCCNT - start_cycles) / (SystemCoreClock / 1000000u);
            s_stats.last_duration_us = duration;
            if (duration > s_stats.max_duration_us) s_stats.max_duration_us = duration;
        }
        if (osMessageQueuePut(s_completion_queue, &completion, 0u, 0u) != osOK) {
            ++s_stats.queue_full;
            if (request.operation == CART_IO_OP_STORAGE_LOAD ||
                request.operation == CART_IO_OP_STORAGE_COMMIT ||
                request.operation == CART_IO_OP_STORAGE_CLEAR) {
                CartTaskBuffer_Release(&completion.result.storage.buffer);
            } else if (request.operation == CART_IO_OP_RESOURCE_INDEX_READ ||
                       request.operation == CART_IO_OP_RESOURCE_BLOB_READ) {
                CartTaskBuffer_Release(&completion.result.buffer);
            }
        }
        if (operation_uses_qflash(request.operation)) {
            taskENTER_CRITICAL();
            if (s_qflash_pending > 0u) --s_qflash_pending;
            taskEXIT_CRITICAL();
        }
        if (operation_uses_sd(request.operation)) {
            taskENTER_CRITICAL();
            if (s_sd_pending > 0u) --s_sd_pending;
            taskEXIT_CRITICAL();
        }
    }
}
