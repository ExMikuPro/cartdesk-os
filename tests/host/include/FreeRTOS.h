#pragma once

#include <stddef.h>

void* pvPortMalloc(size_t size);
void vPortFree(void* pointer);
