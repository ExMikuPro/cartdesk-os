#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t lua_used;
  uint32_t lua_free;
  uint32_t resource_used;
  uint32_t resource_free;
  uint32_t freertos_heap_free;
  uint32_t freertos_heap_min;
} lua_foundation_memory_info_t;

uint64_t lua_foundation_platform_uptime_ms(void);
bool lua_foundation_platform_screen_size(uint32_t* width, uint32_t* height);
const char* lua_foundation_platform_firmware_version(void);
bool lua_foundation_platform_memory_info(lua_foundation_memory_info_t* info);
void lua_foundation_platform_sd_status(bool* available, bool* mounted);
void lua_foundation_platform_usb_status(bool* initialized, bool* configured,
                                        bool* connected);
bool lua_foundation_platform_request_exit(void);
bool lua_foundation_platform_request_restart(void);
