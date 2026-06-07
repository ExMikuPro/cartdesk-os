#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <string.h>

#include "board_gpio.h"

#define LUA_GPIO_LOW   0
#define LUA_GPIO_HIGH  1

#define LUA_GPIO_RISING      0
#define LUA_GPIO_FALLING     1
#define LUA_GPIO_CHANGE      2
#define LUA_GPIO_LOW_LEVEL   3
#define LUA_GPIO_HIGH_LEVEL  4

static const GpioMapEntry *lua_gpio_check_pin(lua_State *L, int index)
{
  const GpioMapEntry *entry = NULL;

  if (lua_type(L, index) == LUA_TNUMBER) {
    lua_Integer id = luaL_checkinteger(L, index);
    luaL_argcheck(L, id >= 0 && id <= UINT8_MAX, index, "invalid gpio id");
    entry = Board_GPIO_FindById((uint8_t)id);
  } else if (lua_type(L, index) == LUA_TSTRING) {
    const char *name = luaL_checkstring(L, index);
    entry = Board_GPIO_FindByName(name);
  } else {
    luaL_argerror(L, index, "expected gpio id or name");
  }

  if (entry == NULL) {
    luaL_argerror(L, index, "unknown or reserved gpio");
  }

  return entry;
}

static int lua_gpio_to_level(lua_State *L, int index)
{
  int type = lua_type(L, index);

  if (type == LUA_TBOOLEAN) {
    return lua_toboolean(L, index) ? LUA_GPIO_HIGH : LUA_GPIO_LOW;
  }

  if (type == LUA_TNUMBER) {
    lua_Integer value = luaL_checkinteger(L, index);
    luaL_argcheck(L, value == LUA_GPIO_LOW || value == LUA_GPIO_HIGH, index,
                  "expected gpio.LOW/gpio.HIGH, 0/1, or boolean");
    return (int)value;
  }

  luaL_argerror(L, index, "expected gpio.LOW/gpio.HIGH, 0/1, or boolean");
  return LUA_GPIO_LOW;
}

static uint8_t lua_gpio_check_mode(lua_State *L, int index)
{
  lua_Integer mode = luaL_checkinteger(L, index);
  luaL_argcheck(L, mode >= BOARD_GPIO_MODE_INPUT && mode <= BOARD_GPIO_MODE_ANALOG,
                index, "invalid gpio mode");
  return (uint8_t)mode;
}

static uint8_t lua_gpio_check_speed(lua_State *L, int index)
{
  lua_Integer speed = luaL_checkinteger(L, index);
  luaL_argcheck(L, speed >= BOARD_GPIO_SPEED_LOW && speed <= BOARD_GPIO_SPEED_HIGH,
                index, "invalid gpio speed");
  return (uint8_t)speed;
}

static int8_t lua_gpio_parse_pull(lua_State *L, int index)
{
  const char *pull = luaL_checkstring(L, index);

  if (strcmp(pull, "none") == 0) {
    return BOARD_GPIO_MODE_INPUT;
  }
  if (strcmp(pull, "up") == 0) {
    return BOARD_GPIO_MODE_INPUT_PULLUP;
  }
  if (strcmp(pull, "down") == 0) {
    return BOARD_GPIO_MODE_INPUT_PULLDOWN;
  }

  luaL_error(L, "gpio.setup: invalid pull value");
  return -1;
}

static void lua_gpio_read_config(lua_State *L, int index, BoardGpioConfig *config)
{
  config->mode = BOARD_GPIO_MODE_INPUT;
  config->speed = BOARD_GPIO_SPEED_LOW;
  config->initial = -1;
  config->pull = -1;

  if (lua_istable(L, index)) {
    lua_getfield(L, index, "mode");
    if (lua_isnil(L, -1)) {
      luaL_error(L, "gpio.setup: config.mode is required");
      return;
    }
    config->mode = lua_gpio_check_mode(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, index, "initial");
    if (!lua_isnil(L, -1)) {
      config->initial = (int8_t)lua_gpio_to_level(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "speed");
    if (!lua_isnil(L, -1)) {
      config->speed = lua_gpio_check_speed(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "pull");
    if (!lua_isnil(L, -1)) {
      config->pull = lua_gpio_parse_pull(L, -1);
    }
    lua_pop(L, 1);
    return;
  }

  config->mode = lua_gpio_check_mode(L, index);
}

static void lua_gpio_push_info(lua_State *L, const GpioMapEntry *entry)
{
  uint32_t caps = entry->caps;

  lua_createtable(L, 0, 11);

  lua_pushinteger(L, entry->id);
  lua_setfield(L, -2, "id");

  lua_pushstring(L, entry->name);
  lua_setfield(L, -2, "name");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_INPUT) != 0u);
  lua_setfield(L, -2, "input");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_OUTPUT) != 0u);
  lua_setfield(L, -2, "output");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_PULLUP) != 0u);
  lua_setfield(L, -2, "pullup");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_PULLDOWN) != 0u);
  lua_setfield(L, -2, "pulldown");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_IRQ) != 0u);
  lua_setfield(L, -2, "irq");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_ADC) != 0u);
  lua_setfield(L, -2, "adc");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_PWM) != 0u);
  lua_setfield(L, -2, "pwm");

  lua_pushboolean(L, (caps & BOARD_GPIO_CAP_OPEN_DRAIN) != 0u);
  lua_setfield(L, -2, "open_drain");

  lua_pushboolean(L, entry->reserved != 0u);
  lua_setfield(L, -2, "reserved");
}

static int lua_gpio_push_status(lua_State *L, int status, const char *operation)
{
  if (status == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }

  if (status == -2) {
    return luaL_error(L, "%s: gpio does not support requested capability", operation);
  }
  if (status == -4) {
    return luaL_error(L, "%s: pin is already used by pwm", operation);
  }

  return luaL_error(L, "%s: invalid gpio", operation);
}

static int l_gpio_count(lua_State *L)
{
  lua_pushinteger(L, (lua_Integer)Board_GPIO_Count());
  return 1;
}

static int l_gpio_list(lua_State *L)
{
  size_t count = Board_GPIO_Count();
  lua_createtable(L, (int)count, 0);

  for (size_t i = 0; i < count; ++i) {
    const GpioMapEntry *entry = Board_GPIO_At(i);
    if (entry == NULL) {
      continue;
    }
    lua_gpio_push_info(L, entry);
    lua_seti(L, -2, (lua_Integer)i + 1);
  }

  return 1;
}

static int l_gpio_info(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  lua_gpio_push_info(L, entry);
  return 1;
}

static int l_gpio_setup(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  BoardGpioConfig config;

  lua_gpio_read_config(L, 2, &config);
  return lua_gpio_push_status(L, Board_GPIO_Setup(entry, &config), "gpio.setup");
}

static int l_gpio_read(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  int level = Board_GPIO_Read(entry);
  if (level < 0) {
    return luaL_error(L, "gpio.read: invalid gpio");
  }

  lua_pushinteger(L, level ? LUA_GPIO_HIGH : LUA_GPIO_LOW);
  return 1;
}

static int l_gpio_write(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  int level = lua_gpio_to_level(L, 2);
  return lua_gpio_push_status(L, Board_GPIO_Write(entry, level), "gpio.write");
}

static int l_gpio_toggle(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  return lua_gpio_push_status(L, Board_GPIO_Toggle(entry), "gpio.toggle");
}

static int l_gpio_release(lua_State *L)
{
  const GpioMapEntry *entry = lua_gpio_check_pin(L, 1);
  return lua_gpio_push_status(L, Board_GPIO_Release(entry), "gpio.release");
}

static int l_gpio_on(lua_State *L)
{
  (void)lua_gpio_check_pin(L, 1);
  (void)luaL_checkinteger(L, 2);
  luaL_checktype(L, 3, LUA_TFUNCTION);

  lua_pushnil(L);
  lua_pushstring(L, "gpio interrupt not supported");
  return 2;
}

static int l_gpio_off(lua_State *L)
{
  (void)lua_gpio_check_pin(L, 1);

  lua_pushnil(L);
  lua_pushstring(L, "gpio interrupt not supported");
  return 2;
}

static int l_global_pin_mode(lua_State *L)
{
  return l_gpio_setup(L);
}

static int l_global_digital_read(lua_State *L)
{
  return l_gpio_read(L);
}

static int l_global_digital_write(lua_State *L)
{
  return l_gpio_write(L);
}

static void set_int_field(lua_State *L, const char *name, lua_Integer value)
{
  lua_pushinteger(L, value);
  lua_setfield(L, -2, name);
}

static const luaL_Reg gpio_lib[] = {
  {"count", l_gpio_count},
  {"list", l_gpio_list},
  {"info", l_gpio_info},
  {"setup", l_gpio_setup},
  {"read", l_gpio_read},
  {"write", l_gpio_write},
  {"toggle", l_gpio_toggle},
  {"release", l_gpio_release},
  {"on", l_gpio_on},
  {"off", l_gpio_off},
  {NULL, NULL}
};

int luaopen_gpio(lua_State *L)
{
  luaL_newlib(L, gpio_lib);

  set_int_field(L, "LOW", LUA_GPIO_LOW);
  set_int_field(L, "HIGH", LUA_GPIO_HIGH);

  set_int_field(L, "INPUT", BOARD_GPIO_MODE_INPUT);
  set_int_field(L, "INPUT_PULLUP", BOARD_GPIO_MODE_INPUT_PULLUP);
  set_int_field(L, "INPUT_PULLDOWN", BOARD_GPIO_MODE_INPUT_PULLDOWN);
  set_int_field(L, "OUTPUT", BOARD_GPIO_MODE_OUTPUT);
  set_int_field(L, "OUTPUT_OPEN_DRAIN", BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN);
  set_int_field(L, "ANALOG", BOARD_GPIO_MODE_ANALOG);

  set_int_field(L, "RISING", LUA_GPIO_RISING);
  set_int_field(L, "FALLING", LUA_GPIO_FALLING);
  set_int_field(L, "CHANGE", LUA_GPIO_CHANGE);
  set_int_field(L, "LOW_LEVEL", LUA_GPIO_LOW_LEVEL);
  set_int_field(L, "HIGH_LEVEL", LUA_GPIO_HIGH_LEVEL);

  set_int_field(L, "SPEED_LOW", BOARD_GPIO_SPEED_LOW);
  set_int_field(L, "SPEED_MEDIUM", BOARD_GPIO_SPEED_MEDIUM);
  set_int_field(L, "SPEED_HIGH", BOARD_GPIO_SPEED_HIGH);

  lua_pushcfunction(L, l_global_pin_mode);
  lua_setglobal(L, "pinMode");
  lua_pushcfunction(L, l_global_digital_read);
  lua_setglobal(L, "digitalRead");
  lua_pushcfunction(L, l_global_digital_write);
  lua_setglobal(L, "digitalWrite");

  return 1;
}
