#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  int  lua_init(void);
  int  lua_init_from_cart(const char *cart_path);
  int  lua_init_from_file(const char *path);
  int  lua_run_bytecode(const void *bytecode, uint32_t len, const char *chunk_name);
  int  lua_run_cart_entry(const char *cart_path);
  int  lua_run_file(const char *path);
  void lua_update_task(void);
  void lua_rt_delay_ms(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
