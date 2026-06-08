#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "xhgc_cart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const uint8_t* data;
  uint32_t size;
  XhgcIndexEntry entry;
} LuaCartCachedResource;

void lua_cart_resource_cache_reset(void);
int lua_cart_resource_cache_preload(const char* cart_path);
bool lua_cart_resource_cache_acquire_image(const char* path,
                                           LuaCartCachedResource* out,
                                           const char** out_err);
void lua_cart_resource_cache_release_image(const uint8_t* data);

#ifdef __cplusplus
}
#endif
