#include "lua_foundation_platform.h"

#include <string.h>

#include "FreeRTOS.h"
#include "diskio.h"
#include "fatfs.h"
#include "lua_runtime_task.h"
#include "lua_vm_memory.h"
#include "lvgl.h"
#include "resource_manager.h"
#include "task.h"
#include "usb_device.h"

#ifndef CARTDESK_FIRMWARE_VERSION
#define CARTDESK_FIRMWARE_VERSION "0.1.0"
#endif

extern USBD_HandleTypeDef hUsbDeviceHS;

uint64_t lua_foundation_platform_uptime_ms(void) {
  static uint32_t last_tick;
  static uint64_t epoch;
  uint32_t tick = (uint32_t)xTaskGetTickCount();
  taskENTER_CRITICAL();
  if (tick < last_tick) epoch += UINT64_C(1) << 32;
  last_tick = tick;
  uint64_t result = epoch | tick;
  taskEXIT_CRITICAL();
  return result;
}

bool lua_foundation_platform_screen_size(uint32_t* width, uint32_t* height) {
  lv_display_t* display = lv_display_get_default();
  if (!display || !width || !height) return false;
  *width = (uint32_t)lv_display_get_horizontal_resolution(display);
  *height = (uint32_t)lv_display_get_vertical_resolution(display);
  return true;
}

const char* lua_foundation_platform_firmware_version(void) {
  return CARTDESK_FIRMWARE_VERSION;
}

bool lua_foundation_platform_memory_info(lua_foundation_memory_info_t* info) {
  if (!info) return false;
  memset(info, 0, sizeof(*info));
  info->lua_used = lua_vm_heap_used();
  uint32_t lua_capacity = lua_vm_heap_capacity();
  info->lua_free = lua_capacity >= info->lua_used ? lua_capacity - info->lua_used : 0u;
  info->resource_used = res_manager_used_bytes();
  uint32_t resource_capacity = res_manager_capacity_bytes();
  info->resource_free = resource_capacity >= info->resource_used
                            ? resource_capacity - info->resource_used : 0u;
  info->freertos_heap_free = (uint32_t)xPortGetFreeHeapSize();
  info->freertos_heap_min = (uint32_t)xPortGetMinimumEverFreeHeapSize();
  return true;
}

void lua_foundation_platform_sd_status(bool* available, bool* mounted) {
  DSTATUS status = disk_status(0u);
  if (available) *available = (status & STA_NODISK) == 0u;
  if (mounted) *mounted = SD_FATFS_IsMounted();
}

void lua_foundation_platform_usb_status(bool* initialized, bool* configured,
                                        bool* connected) {
  if (initialized) *initialized = hUsbDeviceHS.pDesc != NULL;
  if (configured) *configured = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
  if (connected) *connected = hUsbDeviceHS.dev_connection_status != 0u;
}

bool lua_foundation_platform_request_exit(void) {
  LuaRuntimeTask_RequestStop();
  return true;
}

bool lua_foundation_platform_request_restart(void) {
  return LuaRuntimeTask_RequestRestart();
}
