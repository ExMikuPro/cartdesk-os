// lua_gpio.c - Lua GPIO module for STM32H743 (HAL). No pin init.
// API:
//   gpio.write("B", 1, 1/0)
//   gpio.toggle("B", 1)
//   gpio.read("B", 1) -> boolean
//
// Notes:
// - Pin mode (input/output/af/analog) must be configured by CubeMX (MX_GPIO_Init).
// - Optionally enables GPIO port RCC clock for robustness.

#include "lua.h"
#include "lauxlib.h"
#include "stm32h7xx_hal.h"

#include <ctype.h>
#include <stdint.h>

#ifndef LUA_GPIO_ENABLE_RCC
#define LUA_GPIO_ENABLE_RCC 1   // 设为0则模块不碰RCC时钟
#endif

static GPIO_TypeDef* port_from_letter(char c)
{
    c = (char)toupper((unsigned char)c);
    switch (c) {
        case 'A': return GPIOA;
        case 'B': return GPIOB;
        case 'C': return GPIOC;
        case 'D': return GPIOD;
        case 'E': return GPIOE;
        case 'F': return GPIOF;
        case 'G': return GPIOG;
        case 'H': return GPIOH;
#ifdef GPIOI
        case 'I': return GPIOI;
#endif
#ifdef GPIOJ
        case 'J': return GPIOJ;
#endif
#ifdef GPIOK
        case 'K': return GPIOK;
#endif
        default:  return NULL;
    }
}

static void enable_gpio_clock(char c)
{
#if LUA_GPIO_ENABLE_RCC
    c = (char)toupper((unsigned char)c);
    switch (c) {
        case 'A': __HAL_RCC_GPIOA_CLK_ENABLE(); break;
        case 'B': __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        case 'C': __HAL_RCC_GPIOC_CLK_ENABLE(); break;
        case 'D': __HAL_RCC_GPIOD_CLK_ENABLE(); break;
        case 'E': __HAL_RCC_GPIOE_CLK_ENABLE(); break;
        case 'F': __HAL_RCC_GPIOF_CLK_ENABLE(); break;
        case 'G': __HAL_RCC_GPIOG_CLK_ENABLE(); break;
        case 'H': __HAL_RCC_GPIOH_CLK_ENABLE(); break;
#ifdef __HAL_RCC_GPIOI_CLK_ENABLE
        case 'I': __HAL_RCC_GPIOI_CLK_ENABLE(); break;
#endif
#ifdef __HAL_RCC_GPIOJ_CLK_ENABLE
        case 'J': __HAL_RCC_GPIOJ_CLK_ENABLE(); break;
#endif
#ifdef __HAL_RCC_GPIOK_CLK_ENABLE
        case 'K': __HAL_RCC_GPIOK_CLK_ENABLE(); break;
#endif
        default: break;
    }
#else
    (void)c;
#endif
}

static uint16_t pinmask_from_num(int pin)
{
    if (pin < 0 || pin > 15) return 0;
    return (uint16_t)(1u << pin);
}

// 支持 "B" / "PB" / "GPIOB" 等：提取第一个 A~K 字母
static char lua_check_port_letter(lua_State *L, int idx)
{
    const char *s = luaL_checkstring(L, idx);
    if (!s || !s[0]) luaL_error(L, "gpio: invalid port string");

    for (int i = 0; s[i]; ++i) {
        char c = (char)toupper((unsigned char)s[i]);
        if (c >= 'A' && c <= 'K') return c;
    }
    luaL_error(L, "gpio: invalid port string: %s", s);
    return 'A';
}

// gpio.write(port, pin, value)
static int l_gpio_write(lua_State *L)
{
    char pc = lua_check_port_letter(L, 1);
    int pin = (int)luaL_checkinteger(L, 2);
    int v   = lua_toboolean(L, 3);

    GPIO_TypeDef *port = port_from_letter(pc);
    uint16_t mask = pinmask_from_num(pin);
    if (!port || !mask) return luaL_error(L, "gpio.write: invalid port/pin");

    enable_gpio_clock(pc);

    // 原子写：BSRR
    if (v) port->BSRR = mask;
    else   port->BSRR = ((uint32_t)mask << 16);

    return 0;
}

// gpio.toggle(port, pin)
static int l_gpio_toggle(lua_State *L)
{
    char pc = lua_check_port_letter(L, 1);
    int pin = (int)luaL_checkinteger(L, 2);

    GPIO_TypeDef *port = port_from_letter(pc);
    uint16_t mask = pinmask_from_num(pin);
    if (!port || !mask) return luaL_error(L, "gpio.toggle: invalid port/pin");

    enable_gpio_clock(pc);

    HAL_GPIO_TogglePin(port, mask);
    return 0;
}

// gpio.read(port, pin) -> boolean
static int l_gpio_read(lua_State *L)
{
    char pc = lua_check_port_letter(L, 1);
    int pin = (int)luaL_checkinteger(L, 2);

    GPIO_TypeDef *port = port_from_letter(pc);
    uint16_t mask = pinmask_from_num(pin);
    if (!port || !mask) return luaL_error(L, "gpio.read: invalid port/pin");

    enable_gpio_clock(pc);

    GPIO_PinState s = HAL_GPIO_ReadPin(port, mask);
    lua_pushboolean(L, s == GPIO_PIN_SET);
    return 1;
}

static const luaL_Reg gpio_lib[] = {
    {"write",  l_gpio_write},
    {"toggle", l_gpio_toggle},
    {"read",   l_gpio_read},
    {NULL, NULL}
};

int luaopen_gpio(lua_State *L)
{
    luaL_newlib(L, gpio_lib);

    // 可选：导出常量，写 gpio.write(gpio.PORTB,1,1)
    lua_pushstring(L, "A"); lua_setfield(L, -2, "PORTA");
    lua_pushstring(L, "B"); lua_setfield(L, -2, "PORTB");
    lua_pushstring(L, "C"); lua_setfield(L, -2, "PORTC");
    lua_pushstring(L, "D"); lua_setfield(L, -2, "PORTD");
    lua_pushstring(L, "E"); lua_setfield(L, -2, "PORTE");
    lua_pushstring(L, "F"); lua_setfield(L, -2, "PORTF");
    lua_pushstring(L, "G"); lua_setfield(L, -2, "PORTG");
    lua_pushstring(L, "H"); lua_setfield(L, -2, "PORTH");
#ifdef GPIOI
    lua_pushstring(L, "I"); lua_setfield(L, -2, "PORTI");
#endif
#ifdef GPIOJ
    lua_pushstring(L, "J"); lua_setfield(L, -2, "PORTJ");
#endif
#ifdef GPIOK
    lua_pushstring(L, "K"); lua_setfield(L, -2, "PORTK");
#endif

    return 1;
}
