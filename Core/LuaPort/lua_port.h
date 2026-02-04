#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 这里仅保存“Lua 侧需要控制的硬件句柄/资源”。
// 头文件不依赖 HAL 类型，避免污染上层；实现文件里再 cast 成 GPIO_TypeDef* 等。
typedef struct {
  void*    led_port;   // GPIO_TypeDef*
  uint16_t led_pin;    // GPIO_PIN_x
} lua_port_gpio_t;

typedef struct {
  lua_port_gpio_t gpio;
} lua_port_config_t;

struct lua_State;

/**
 * 注册 Lua 与底层硬件交互入口（全部在 Core/LuaPort 内实现）：
 *   - gpio.xxx   (表)
 *   - delay(ms)  (全局函数，底层直接 HAL_Delay)
 *   - tim.xxx    (表，可选：us()/delay_us())
 */
void lua_port_bind(struct lua_State* L, const lua_port_config_t* cfg);

#ifdef __cplusplus
}
#endif
