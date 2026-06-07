#include <stdint.h>

#include "FreeRTOS.h"

__attribute__((section(".ram_runtime"), aligned(32)))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];
