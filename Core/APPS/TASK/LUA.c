#include "lua_vm.h"
#include "cartdesk_task.h"


uint32_t TaskTicks_LUA = 0;
static uint8_t s_lua_ready = 0;
static volatile uint8_t s_lua_start_requested = 0;
static const char *s_lua_cart_path = "0:/cart.bin";

void Task_LUA_StartCart(const char *cart_path) {
  if (s_lua_ready) {
    return;
  }

  if (cart_path != NULL && cart_path[0] != '\0') {
    s_lua_cart_path = cart_path;
  }

  s_lua_start_requested = 1;
  TaskTicks_LUA = 0;
}

uint8_t Task_LUA_IsRunning(void) {
  return s_lua_ready;
}

void Task_LUA() {
  if (!s_lua_ready && !s_lua_start_requested) {
    TaskTicks_LUA = 50;
    return;
  }

  if (!s_lua_ready) {
    if (lua_init_from_cart(s_lua_cart_path) != 0) {
      TaskTicks_LUA = 1000;
      return;
    }
    s_lua_ready = 1;
    s_lua_start_requested = 0;
  }

  if (TaskTicks_LUA == 0) {
    lua_update_task();
    TaskTicks_LUA = 10;
  }
}
