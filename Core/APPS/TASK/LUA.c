#include "lua_vm.h"
#include "Task.h"


uint32_t TaskTicks_LUA = 0;
uint8_t LUA_Flag = 0;

void Task_LUA() {
  switch (LUA_Flag) {
    case 0:
      if (TaskTicks_LUA == 0) {
        lua_update_task();
        LUA_Flag = 1;
        TaskTicks_LUA = 10;
      }
      break;
    case 1:
      if (TaskTicks_LUA == 0) {
        lua_update_task();
        LUA_Flag = 0;
        TaskTicks_LUA = 10;
      }
      break;

  }
}