#include "cart_log.h"

#include <stdbool.h>
#include <stdio.h>

#include "lua_foundation_platform.h"
#include "usart.h"

#define CART_LOG_QUEUE_DEPTH 4u
#define CART_LOG_LINE_MAX 320u

typedef struct {
  uint16_t length;
  char text[CART_LOG_LINE_MAX];
} cart_log_slot_t;

static cart_log_slot_t g_queue[CART_LOG_QUEUE_DEPTH];
static uint8_t g_head;
static uint8_t g_tail;
static uint8_t g_count;
static bool g_in_flight;
static uint32_t g_dropped;

void CartLog_Write(cart_log_level_t level, const char* tag, const char* message) {
  static const char* const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  if (g_count >= CART_LOG_QUEUE_DEPTH) {
    ++g_dropped;
    return;
  }
  const char* level_name = level <= CART_LOG_ERROR ? names[level] : "INFO";
  cart_log_slot_t* slot = &g_queue[g_head];
  uint32_t dropped = g_dropped;
  g_dropped = 0u;
  int length = dropped == 0u
      ? snprintf(slot->text, sizeof(slot->text), "[%llu][%s][%s] %s\r\n",
                 (unsigned long long)lua_foundation_platform_uptime_ms(),
                 level_name, tag && tag[0] ? tag : "unknown",
                 message ? message : "")
      : snprintf(slot->text, sizeof(slot->text),
                 "[%llu][%s][%s][transport_dropped=%lu] %s\r\n",
                 (unsigned long long)lua_foundation_platform_uptime_ms(),
                 level_name, tag && tag[0] ? tag : "unknown",
                 (unsigned long)dropped, message ? message : "");
  if (length < 0) return;
  slot->length = (uint16_t)((size_t)length < sizeof(slot->text)
                                ? (size_t)length : sizeof(slot->text) - 1u);
  g_head = (uint8_t)((g_head + 1u) % CART_LOG_QUEUE_DEPTH);
  ++g_count;
}

void CartLog_Process(void) {
  if (g_in_flight) {
    if (HAL_UART_GetState(&huart1) != HAL_UART_STATE_READY) return;
    g_tail = (uint8_t)((g_tail + 1u) % CART_LOG_QUEUE_DEPTH);
    if (g_count > 0u) --g_count;
    g_in_flight = false;
  }
  if (g_count == 0u || huart1.Instance != USART1) return;
  cart_log_slot_t* slot = &g_queue[g_tail];
  if (HAL_UART_Transmit_IT(&huart1, (uint8_t*)slot->text, slot->length) == HAL_OK)
    g_in_flight = true;
}
