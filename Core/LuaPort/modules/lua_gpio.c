#include "lua.h"
#include "lauxlib.h"
#include "lua_port.h"

#include "stm32h7xx_hal.h"

static const lua_port_config_t* port_get_cfg(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, "port.cfg");
  const lua_port_config_t* cfg = (const lua_port_config_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return cfg;
}

static int l_gpio_write(lua_State* L)
{
  const lua_port_config_t* cfg = port_get_cfg(L);
  int v = lua_toboolean(L, 1);

  HAL_GPIO_WritePin((GPIO_TypeDef*)cfg->gpio.led_port,
                    cfg->gpio.led_pin,
                    v ? GPIO_PIN_SET : GPIO_PIN_RESET);
  return 0;
}

static int l_gpio_toggle(lua_State* L)
{
  (void)L;
  const lua_port_config_t* cfg = port_get_cfg(L);
  HAL_GPIO_TogglePin((GPIO_TypeDef*)cfg->gpio.led_port, cfg->gpio.led_pin);
  return 0;
}

static int l_gpio_read(lua_State* L)
{
  const lua_port_config_t* cfg = port_get_cfg(L);
  GPIO_PinState s = HAL_GPIO_ReadPin((GPIO_TypeDef*)cfg->gpio.led_port, cfg->gpio.led_pin);
  lua_pushboolean(L, s == GPIO_PIN_SET);
  return 1;
}

static const luaL_Reg gpio_funcs[] = {
  {"write",  l_gpio_write},
  {"toggle", l_gpio_toggle},
  {"read",   l_gpio_read},
  {NULL, NULL}
};

int luaopen_gpio(lua_State* L)
{
  luaL_newlib(L, gpio_funcs);
  return 1;
}
