#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CART_BUFFER_SOURCE_NONE = 0,
    CART_BUFFER_SOURCE_CALLER,
    CART_BUFFER_SOURCE_RESOURCE_ARENA,
    CART_BUFFER_SOURCE_DMA_POOL,
    CART_BUFFER_SOURCE_COLD_POOL,
    CART_BUFFER_SOURCE_RTOS_HEAP,
} cart_buffer_source_t;

typedef struct {
    void *data;
    uint32_t capacity;
    uint32_t length;
    uint32_t owner_id;
    cart_buffer_source_t source;
} cart_task_buffer_t;

typedef enum {
    CART_IO_OP_NONE = 0,
    CART_IO_OP_INITIALIZE,
    CART_IO_OP_CRASH_LOG_APPEND,
    CART_IO_OP_CART_PROBE,
    CART_IO_OP_CART_READ_INFO,
    CART_IO_OP_CART_READ_RESOURCE,
    CART_IO_OP_LAUNCHER_STORE_LOAD,
    CART_IO_OP_LAUNCHER_STORE_READ_ICON,
    CART_IO_OP_LAUNCHER_STORE_UPSERT,
    CART_IO_OP_STORAGE_LOAD,
    CART_IO_OP_STORAGE_COMMIT,
    CART_IO_OP_STORAGE_CLEAR,
} cart_io_operation_t;

typedef struct {
    uint32_t request_id;
    uint32_t owner_id;
    cart_io_operation_t operation;
    uint32_t timeout_ms;
    uint32_t submitted_tick;
    union {
        struct {
            char path[32];
            cart_task_buffer_t output;
        } cart;
        struct {
            uint8_t slot;
            uint8_t reserved[3];
            cart_task_buffer_t output;
        } launcher_read_icon;
        struct {
            cart_task_buffer_t info;
            cart_task_buffer_t icon;
        } launcher_upsert;
        struct {
            uint64_t cart_id;
            uint16_t entry_count;
            uint16_t reserved;
            cart_task_buffer_t payload;
        } storage;
    } params;
} cart_io_request_t;

typedef enum {
    CART_IO_STATUS_OK = 0,
    CART_IO_STATUS_NOT_FOUND,
    CART_IO_STATUS_INVALID_ARGUMENT,
    CART_IO_STATUS_TIMEOUT,
    CART_IO_STATUS_NOT_READY,
    CART_IO_STATUS_NO_MEMORY,
    CART_IO_STATUS_IO_ERROR,
    CART_IO_STATUS_CORRUPT,
    CART_IO_STATUS_CANCELLED,
} cart_io_status_t;

typedef struct {
    uint32_t request_id;
    uint32_t owner_id;
    cart_io_operation_t operation;
    cart_io_status_t status;
    union {
        cart_task_buffer_t buffer;
        uint32_t value;
        uint8_t launcher_slot;
        struct {
            cart_task_buffer_t buffer;
            uint16_t entry_count;
            uint16_t reserved;
        } storage;
    } result;
} cart_io_completion_t;

typedef enum {
    CART_AUDIO_CMD_NONE = 0,
    CART_AUDIO_CMD_STOP,
    CART_AUDIO_CMD_RESET,
} cart_audio_command_type_t;

typedef enum {
    CART_AUDIO_STATE_IDLE = 0,
    CART_AUDIO_STATE_STOPPED,
    CART_AUDIO_STATE_RESET,
} cart_audio_state_t;

typedef struct {
    uint32_t request_id;
    uint32_t owner_id;
    cart_audio_command_type_t command;
} cart_audio_command_t;

typedef struct {
    uint32_t request_id;
    uint32_t owner_id;
    cart_audio_command_type_t command;
    cart_audio_state_t state;
} cart_audio_completion_t;

typedef struct {
    uint32_t heartbeat;
    uint32_t last_heartbeat_tick;
    uint32_t processed;
    uint32_t failed;
    uint32_t timeout;
    uint32_t queue_full;
    uint32_t stale_completion;
    uint32_t max_queue_depth;
    uint32_t last_duration_us;
    uint32_t max_duration_us;
    uint32_t stack_high_water;
} cart_task_stats_t;

_Static_assert(sizeof(cart_io_request_t) <= 96u,
               "IO request messages must remain small");
_Static_assert(sizeof(cart_io_completion_t) <= 48u,
               "IO completion messages must remain small");
