#pragma once

#include <stdint.h>

typedef enum {
  CART_LOG_DEBUG = 0,
  CART_LOG_INFO,
  CART_LOG_WARN,
  CART_LOG_ERROR,
} cart_log_level_t;

void CartLog_Write(cart_log_level_t level, const char* tag, const char* message);
void CartLog_Process(void);
