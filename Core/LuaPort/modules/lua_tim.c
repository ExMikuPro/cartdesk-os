#include "lua.h"
#include "lauxlib.h"
#include "stm32h7xx_hal.h"
#include <stdint.h>

// 轻量 tim 模块：用 DWT CYCCNT 提供微秒计时/延时（不占用具体 TIM 外设）
// Lua: tim.us() / tim.delay_us(us)
static uint8_t s_dwt_inited = 0;

static void dwt_init_once(void)
{
  if (s_dwt_inited) return;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  s_dwt_inited = 1;
}

static inline uint32_t cycles_per_us(void)
{
  // SystemCoreClock 由 HAL 维护
  return (uint32_t)(SystemCoreClock / 1000000u);
}

static int l_tim_us(lua_State* L)
{
  dwt_init_once();
  uint32_t c = DWT->CYCCNT;
  uint64_t us = (uint64_t)c / (uint64_t)cycles_per_us();
  lua_pushinteger(L, (lua_Integer)us);
  return 1;
}

static int l_tim_delay_us(lua_State* L)
{
  lua_Integer us = luaL_checkinteger(L, 1);
  if (us <= 0) return 0;

  dwt_init_once();
  uint32_t start = DWT->CYCCNT;
  uint32_t target = (uint32_t)us * cycles_per_us();

  while ((uint32_t)(DWT->CYCCNT - start) < target) {
    __NOP();
  }
  return 0;
}

static const luaL_Reg tim_funcs[] = {
  {"us",       l_tim_us},
  {"delay_us", l_tim_delay_us},
  {NULL, NULL}
};

int luaopen_tim(lua_State* L)
{
  luaL_newlib(L, tim_funcs);
  return 1;
}
