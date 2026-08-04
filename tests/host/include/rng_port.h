#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
  RNG_OK = 0,
  RNG_E_TEST_FAILURE = 1,
} RNG_Status;

RNG_Status RNG_GetU32(uint32_t* out, void* error_info);
RNG_Status RNG_Fill(void* buffer, size_t length, void* error_info);
