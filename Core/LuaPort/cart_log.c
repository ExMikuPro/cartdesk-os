#include "cart_log.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "lua_foundation_platform.h"
#include "usart.h"

typedef struct {
  uint32_t timestamp_ms;
  cart_log_level_t level;
  char tag[CART_LOG_TAG_MAX];
  char message[CART_LOG_MESSAGE_MAX];
} cart_log_slot_t;

static osMessageQueueId_t g_queue;
static uint32_t g_dropped;
static uint32_t g_max_queue_depth;

bool CartLog_Init(void) {
  g_dropped = 0u;
  g_max_queue_depth = 0u;
  g_queue = osMessageQueueNew(CART_LOG_QUEUE_DEPTH, sizeof(cart_log_slot_t), NULL);
  return g_queue != NULL;
}

bool CartLog_TryWrite(cart_log_level_t level, const char* tag, const char* message) {
  if (g_queue == NULL) return false;
  cart_log_slot_t slot = {
      .timestamp_ms = (uint32_t)lua_foundation_platform_uptime_ms(),
      .level = level <= CART_LOG_ERROR ? level : CART_LOG_INFO,
  };
  (void)snprintf(slot.tag, sizeof(slot.tag), "%s", tag && tag[0] ? tag : "unknown");
  (void)snprintf(slot.message, sizeof(slot.message), "%s", message ? message : "");
  if (osMessageQueuePut(g_queue, &slot, 0u, 0u) != osOK) {
    ++g_dropped;
    return false;
  }
  uint32_t depth = osMessageQueueGetCount(g_queue);
  if (depth > g_max_queue_depth) g_max_queue_depth = depth;
  return true;
}

void CartLog_Write(cart_log_level_t level, const char* tag, const char* message) {
  (void)CartLog_TryWrite(level, tag, message);
}

bool CartLog_ProcessOne(uint32_t timeout_ms) {
  static const char* const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  cart_log_slot_t slot;
  if (g_queue == NULL || osMessageQueueGet(g_queue, &slot, NULL, timeout_ms) != osOK) {
    return false;
  }
  char line[224];
  int length = snprintf(line, sizeof(line), "[%lu][%s][%s] %s\r\n",
                        (unsigned long)slot.timestamp_ms, names[slot.level],
                        slot.tag, slot.message);
  if (length > 0 && huart1.Instance == USART1) {
    uint16_t size = (uint16_t)((size_t)length < sizeof(line) ? (size_t)length
                                                             : sizeof(line) - 1u);
    (void)HAL_UART_Transmit(&huart1, (uint8_t*)line, size, 100u);
  }
  return true;
}

uint32_t CartLog_DroppedCount(void) {
  return g_dropped;
}

uint32_t CartLog_MaxQueueDepth(void) {
  return g_max_queue_depth;
}
