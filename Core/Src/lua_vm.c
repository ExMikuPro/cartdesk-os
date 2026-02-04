#include "lua_vm.h"

#include "main.h"
#include "lua_port.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* allocator（裸机版） */
static void* lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;
  if (nsize == 0) { free(ptr); return NULL; }
  return realloc(ptr, nsize);
}

/* 最小开库（别用 luaL_openlibs） */
static void lua_openlibs_min(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);                lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);     lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);    lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);     lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);     lua_pop(L, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);  lua_pop(L, 1);
}

/* print（你后续可以替换成 UART/RTT logger） */
static int l_print(lua_State* L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, i, &len);
    fwrite(s, 1, len, stdout);
    if (i < n) fwrite("\t", 1, 1, stdout);
    lua_pop(L, 1);
  }
  fwrite("\r\n", 1, 2, stdout);
  return 0;
}

static int lua_run_buffer(lua_State* L, const char* buf, size_t len, const char* name) {
  int st = luaL_loadbufferx(L, buf, len, name, NULL);
  if (st != LUA_OK) return st;
  return lua_pcall(L, 0, LUA_MULTRET, 0);
}

static const char* DEMO_LUA =
"gpio.write(false)\n"
"while true do\n"
"  gpio.toggle()\n"
"  delay(200)\n"
"end\n";


void lua_demo_blink(void) {
  lua_State* L = lua_newstate(lua_alloc, NULL);
  if (!L) return;

  lua_openlibs_min(L);

  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");

  // 绑定底层硬件（全部在 Core/LuaPort 内）
  static lua_port_config_t cfg;
  cfg.gpio.led_port = (void*)LED_GPIO_Port;
  cfg.gpio.led_pin  = (uint16_t)LED_Pin;
  lua_port_bind(L, &cfg);

  int st = lua_run_buffer(L, DEMO_LUA, strlen(DEMO_LUA), "demo");
  if (st != LUA_OK) {
    const char* e = lua_tostring(L, -1);
    printf("lua err: %s\r\n", e ? e : "(nil)");
    lua_pop(L, 1);
  }

  lua_close(L);
}
