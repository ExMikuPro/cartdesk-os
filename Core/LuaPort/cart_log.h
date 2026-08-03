#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CART_LOG_QUEUE_DEPTH 24u
#define CART_LOG_TAG_MAX 16u
#define CART_LOG_MESSAGE_MAX 160u

typedef enum {
  CART_LOG_DEBUG = 0,
  CART_LOG_INFO,
  CART_LOG_WARN,
  CART_LOG_ERROR,
} cart_log_level_t;

bool CartLog_Init(void);
bool CartLog_TryWrite(cart_log_level_t level, const char* tag, const char* message);
void CartLog_Write(cart_log_level_t level, const char* tag, const char* message);
bool CartLog_ProcessOne(uint32_t timeout_ms);
uint32_t CartLog_DroppedCount(void);
uint32_t CartLog_MaxQueueDepth(void);
