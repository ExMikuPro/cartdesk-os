#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "task_messages.h"

#define CART_IO_REQUEST_QUEUE_DEPTH 16u
#define CART_IO_COMPLETION_QUEUE_DEPTH 16u

#define CART_IO_TIMEOUT_CART_HEADER_MS 500u
#define CART_IO_TIMEOUT_SD_READ_MS 2000u
#define CART_IO_TIMEOUT_SD_WRITE_MS 3000u
#define CART_IO_TIMEOUT_LFS_COMMIT_MS 3000u
#define CART_IO_READY_TIMEOUT_MS 5000u

bool CartIoService_Init(void);
bool CartIoService_WorkerInitialize(void);
void CartIoService_WorkerRun(void);

uint32_t CartIoService_NextRequestId(void);
bool CartIoService_Submit(const cart_io_request_t *request, uint32_t timeout_ms);
bool CartIoService_TryReceive(cart_io_completion_t *completion);
bool CartIoService_CancelOwner(uint32_t owner_id);
bool CartIoService_IsReady(void);
bool CartIoService_WaitReady(uint32_t timeout_ms);
bool CartIoService_IsQflashExclusive(void);
bool CartIoService_BeginSdExclusive(void);
void CartIoService_EndSdExclusive(void);
bool CartIoService_IsSdExclusive(void);
void CartIoService_GetStats(cart_task_stats_t *stats);
void CartTaskBuffer_Release(cart_task_buffer_t *buffer);
