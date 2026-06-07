#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>

#include "board_gpio.h"
#include "board_pwm.h"

static const BoardPwmEntry *lua_pwm_get_pin(lua_State *L, int index)
{
  if (lua_type(L, index) == LUA_TNUMBER) {
    lua_Integer id = luaL_checkinteger(L, index);
    if (id < 0 || id > UINT8_MAX) {
      return NULL;
    }
    return Board_PWM_FindById((uint8_t)id);
  }

  if (lua_type(L, index) == LUA_TSTRING) {
    return Board_PWM_FindByName(lua_tostring(L, index));
  }

  return NULL;
}

static int lua_pwm_fail(lua_State *L, const char *message)
{
  lua_pushnil(L);
  lua_pushstring(L, message);
  return 2;
}

static int lua_pwm_status(lua_State *L, int status, const BoardPwmEntry *entry)
{
  if (status == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }

  switch (status) {
    case -2:
      return lua_pwm_fail(L, "pin is reserved");
    case -3:
      return lua_pwm_fail(L, "pin does not support pwm");
    case -4: {
      const GpioMapEntry *gpio = entry ? Board_GPIO_FindById(entry->id) : NULL;
      BoardPinOwner owner = Board_GPIO_GetOwner(gpio);
      if (owner == BOARD_PIN_OWNER_GPIO) {
        return lua_pwm_fail(L, "pin is already used by gpio");
      }
      if (owner == BOARD_PIN_OWNER_ADC) {
        return lua_pwm_fail(L, "pin is already used by adc");
      }
      return lua_pwm_fail(L, "pin is already used");
    }
    case -5:
      return lua_pwm_fail(L, "invalid pwm frequency");
    case -6:
      return lua_pwm_fail(L, "pwm backend not supported");
    case -7:
      return lua_pwm_fail(L, "pwm is not configured");
    default:
      return lua_pwm_fail(L, "invalid pwm pin");
  }
}

static int lua_pwm_read_freq(lua_State *L, int index, uint32_t *out_freq)
{
  if (out_freq == NULL || !lua_isnumber(L, index)) {
    return -1;
  }

  lua_Number freq = lua_tonumber(L, index);
  if (freq <= 0 || freq > (lua_Number)UINT32_MAX) {
    return -1;
  }

  *out_freq = (uint32_t)(freq + (lua_Number)0.5);
  return 0;
}

static int lua_pwm_read_duty(lua_State *L, int index, uint8_t *out_duty)
{
  if (out_duty == NULL || !lua_isnumber(L, index)) {
    return -1;
  }

  lua_Number value = lua_tonumber(L, index);
  if (value < (lua_Number)BOARD_PWM_MIN_DUTY || value > (lua_Number)BOARD_PWM_MAX_DUTY) {
    return -1;
  }

  *out_duty = (uint8_t)(value + (lua_Number)0.5);
  return 0;
}

static int lua_pwm_read_config(lua_State *L, int index, BoardPwmConfig *config)
{
  config->freq = BOARD_PWM_DEFAULT_FREQ;
  config->duty = 0u;
  config->start = 0u;

  if (lua_istable(L, index)) {
    lua_getfield(L, index, "freq");
    if (!lua_isnil(L, -1)) {
      if (lua_pwm_read_freq(L, -1, &config->freq) != 0) {
        lua_pop(L, 1);
        return -5;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "duty");
    if (!lua_isnil(L, -1)) {
      if (lua_pwm_read_duty(L, -1, &config->duty) != 0) {
        lua_pop(L, 1);
        return -8;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "start");
    if (!lua_isnil(L, -1)) {
      config->start = lua_toboolean(L, -1) ? 1u : 0u;
    }
    lua_pop(L, 1);
    return 0;
  }

  if (lua_pwm_read_freq(L, index, &config->freq) != 0) {
    return -5;
  }
  config->duty = 0u;
  config->start = 0u;
  return 0;
}

static void lua_pwm_push_info(lua_State *L, const BoardPwmEntry *entry)
{
  BoardPwmState state = {0};
  (void)Board_PWM_GetState(entry, &state);

  lua_createtable(L, 0, 11);

  lua_pushinteger(L, entry->id);
  lua_setfield(L, -2, "id");

  lua_pushstring(L, entry->name);
  lua_setfield(L, -2, "name");

  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "pwm");

  lua_pushinteger(L, entry->min_freq);
  lua_setfield(L, -2, "min_freq");

  lua_pushinteger(L, entry->max_freq);
  lua_setfield(L, -2, "max_freq");

  lua_pushinteger(L, entry->duty_bits);
  lua_setfield(L, -2, "duty_bits");

  lua_pushinteger(L, entry->shared_group);
  lua_setfield(L, -2, "shared_group");

  lua_pushboolean(L, state.configured != 0u);
  lua_setfield(L, -2, "configured");

  lua_pushboolean(L, state.running != 0u);
  lua_setfield(L, -2, "running");

  lua_pushinteger(L, state.freq);
  lua_setfield(L, -2, "freq");

  lua_pushinteger(L, state.duty);
  lua_setfield(L, -2, "duty");
}

static int l_pwm_count(lua_State *L)
{
  lua_pushinteger(L, (lua_Integer)Board_PWM_Count());
  return 1;
}

static int l_pwm_list(lua_State *L)
{
  size_t count = Board_PWM_Count();
  lua_createtable(L, (int)count, 0);

  for (size_t i = 0; i < count; ++i) {
    const BoardPwmEntry *entry = Board_PWM_At(i);
    if (entry == NULL) {
      continue;
    }
    lua_pwm_push_info(L, entry);
    lua_seti(L, -2, (lua_Integer)i + 1);
  }

  return 1;
}

static int l_pwm_info(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  lua_pwm_push_info(L, entry);
  return 1;
}

static int l_pwm_setup(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  BoardPwmConfig config;
  int config_status = lua_pwm_read_config(L, 2, &config);
  if (config_status == -5) {
    return lua_pwm_fail(L, "invalid pwm frequency");
  }
  if (config_status == -8) {
    return lua_pwm_fail(L, "invalid pwm duty");
  }
  return lua_pwm_status(L, Board_PWM_Setup(entry, &config), entry);
}

static int l_pwm_write(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  uint8_t duty = 0u;
  if (lua_pwm_read_duty(L, 2, &duty) != 0) {
    return lua_pwm_fail(L, "invalid pwm duty");
  }
  return lua_pwm_status(L, Board_PWM_Write(entry, duty), entry);
}

static int l_pwm_read(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  uint8_t duty = 0u;
  int status = Board_PWM_Read(entry, &duty);
  if (status != 0) {
    return lua_pwm_status(L, status, entry);
  }

  lua_pushinteger(L, duty);
  return 1;
}

static int l_pwm_set_freq(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  uint32_t freq = 0u;
  if (lua_pwm_read_freq(L, 2, &freq) != 0) {
    return lua_pwm_fail(L, "invalid pwm frequency");
  }
  return lua_pwm_status(L, Board_PWM_SetFreq(entry, freq), entry);
}

static int l_pwm_get_freq(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  uint32_t freq = 0u;
  int status = Board_PWM_GetFreq(entry, &freq);
  if (status != 0) {
    return lua_pwm_status(L, status, entry);
  }

  lua_pushinteger(L, freq);
  return 1;
}

static int l_pwm_stop(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  return lua_pwm_status(L, Board_PWM_Stop(entry), entry);
}

static int l_pwm_release(lua_State *L)
{
  const BoardPwmEntry *entry = lua_pwm_get_pin(L, 1);
  if (entry == NULL) {
    return lua_pwm_fail(L, "invalid pwm pin");
  }

  return lua_pwm_status(L, Board_PWM_Release(entry), entry);
}

static void set_int_field(lua_State *L, const char *name, lua_Integer value)
{
  lua_pushinteger(L, value);
  lua_setfield(L, -2, name);
}

static const luaL_Reg pwm_lib[] = {
  {"count", l_pwm_count},
  {"list", l_pwm_list},
  {"info", l_pwm_info},
  {"setup", l_pwm_setup},
  {"write", l_pwm_write},
  {"read", l_pwm_read},
  {"setFreq", l_pwm_set_freq},
  {"getFreq", l_pwm_get_freq},
  {"stop", l_pwm_stop},
  {"release", l_pwm_release},
  {NULL, NULL}
};

int luaopen_pwm(lua_State *L)
{
  luaL_newlib(L, pwm_lib);

  set_int_field(L, "MIN", BOARD_PWM_MIN_DUTY);
  set_int_field(L, "MAX", BOARD_PWM_MAX_DUTY);
  set_int_field(L, "DEFAULT_FREQ", BOARD_PWM_DEFAULT_FREQ);

  set_int_field(L, "POLARITY_HIGH", 0);
  set_int_field(L, "POLARITY_LOW", 1);

  return 1;
}
