#include "board_pwm.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "board_gpio.h"
#include "tim.h"

#define BOARD_PWM_DUTY_STEPS 255u
#define BOARD_PWM_TIMER_MAX_PERIOD 65535u
#define BOARD_PWM_TIMER_GROUP_TIM3 1u
#define BOARD_PWM_TIMER_GROUP_TIM2 2u

typedef struct {
  uint8_t configured;
  uint8_t running;
  uint8_t duty;
} PwmChannelState;

static const BoardPwmEntry k_pwm_map[] = {
  {0, "GPIO0", TIM3, TIM_CHANNEL_1, 1u, 100000u, 16u, BOARD_PWM_TIMER_GROUP_TIM3, 0},
  {1, "GPIO1", TIM3, TIM_CHANNEL_2, 1u, 100000u, 16u, BOARD_PWM_TIMER_GROUP_TIM3, 0},
  {2, "GPIO2", TIM3, TIM_CHANNEL_3, 1u, 100000u, 16u, BOARD_PWM_TIMER_GROUP_TIM3, 0},
  {3, "GPIO3", TIM3, TIM_CHANNEL_4, 1u, 100000u, 16u, BOARD_PWM_TIMER_GROUP_TIM3, 0},
  {4, "GPIO4", TIM2, TIM_CHANNEL_4, 1u, 100000u, 16u, BOARD_PWM_TIMER_GROUP_TIM2, 0},
};

static PwmChannelState s_pwm_state[sizeof(k_pwm_map) / sizeof(k_pwm_map[0])];
static uint8_t s_tim3_configured;
static uint32_t s_tim3_freq;
static uint8_t s_tim2_configured;
static uint32_t s_tim2_freq;

static int name_equals(const char *a, const char *b)
{
  if (a == NULL || b == NULL) {
    return 0;
  }

  while (*a != '\0' && *b != '\0') {
    if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
      return 0;
    }
    ++a;
    ++b;
  }

  return *a == '\0' && *b == '\0';
}

static int entry_index(const BoardPwmEntry *entry)
{
  if (entry == NULL) {
    return -1;
  }

  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (entry == &k_pwm_map[i]) {
      return (int)i;
    }
  }

  return -1;
}

static const GpioMapEntry *gpio_for_pwm(const BoardPwmEntry *entry)
{
  return entry ? Board_GPIO_FindById(entry->id) : NULL;
}

static uint32_t tim3_clock_hz(void)
{
  RCC_ClkInitTypeDef clock_config = {0};
  uint32_t flash_latency = 0;
  HAL_RCC_GetClockConfig(&clock_config, &flash_latency);

  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  if (clock_config.APB1CLKDivider != RCC_HCLK_DIV1) {
    pclk1 *= 2u;
  }
  return pclk1;
}

static TIM_HandleTypeDef *timer_handle(const BoardPwmEntry *entry)
{
  if (entry != NULL && entry->timer == TIM2) {
    return &htim2;
  }
  return &htim3;
}

static uint8_t *timer_configured_flag(const BoardPwmEntry *entry)
{
  return (entry != NULL && entry->timer == TIM2) ? &s_tim2_configured : &s_tim3_configured;
}

static uint32_t *timer_frequency(const BoardPwmEntry *entry)
{
  return (entry != NULL && entry->timer == TIM2) ? &s_tim2_freq : &s_tim3_freq;
}

static int timer_configure_frequency(const BoardPwmEntry *entry, uint32_t freq)
{
  if (freq == 0u) {
    return -5;
  }

  uint32_t timer_clock = tim3_clock_hz();
  uint32_t prescaler = 0u;
  uint32_t period = (timer_clock / freq);

  if (period == 0u) {
    return -5;
  }

  while (period > BOARD_PWM_TIMER_MAX_PERIOD + 1u) {
    ++prescaler;
    if (prescaler > 0xFFFFu) {
      return -5;
    }
    period = timer_clock / ((prescaler + 1u) * freq);
    if (period == 0u) {
      return -5;
    }
  }

  if (period < 2u) {
    return -5;
  }

  TIM_HandleTypeDef *timer = timer_handle(entry);
  if (entry->timer == TIM2) {
    HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_4);
  } else {
    HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_3);
    HAL_TIM_PWM_Stop(timer, TIM_CHANNEL_4);
  }

  timer->Instance = (TIM_TypeDef *)entry->timer;
  timer->Init.Prescaler = prescaler;
  timer->Init.CounterMode = TIM_COUNTERMODE_UP;
  timer->Init.Period = period - 1u;
  timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(timer) != HAL_OK) {
    return -6;
  }

  TIM_OC_InitTypeDef oc = {0};
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;

  if (entry->timer == TIM2) {
    if (HAL_TIM_PWM_ConfigChannel(timer, &oc, TIM_CHANNEL_4) != HAL_OK) {
      return -6;
    }
  } else {
    if (HAL_TIM_PWM_ConfigChannel(timer, &oc, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(timer, &oc, TIM_CHANNEL_2) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(timer, &oc, TIM_CHANNEL_3) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(timer, &oc, TIM_CHANNEL_4) != HAL_OK) {
      return -6;
    }
  }

  *timer_configured_flag(entry) = 1u;
  *timer_frequency(entry) = freq;
  return 0;
}

static void configure_pwm_pin(const BoardPwmEntry *entry)
{
  const GpioMapEntry *gpio = gpio_for_pwm(entry);
  if (gpio == NULL) {
    return;
  }

  GPIO_InitTypeDef init = {0};
  init.Pin = gpio->pin;
  init.Mode = GPIO_MODE_AF_PP;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  init.Alternate = entry->timer == TIM2 ? GPIO_AF1_TIM2 : GPIO_AF2_TIM3;
  HAL_GPIO_Init((GPIO_TypeDef *)gpio->port, &init);
}

static uint32_t duty_to_pulse(const BoardPwmEntry *entry, uint8_t duty)
{
  uint32_t period = __HAL_TIM_GET_AUTORELOAD(timer_handle(entry));
  return ((period + 1u) * (uint32_t)duty) / BOARD_PWM_DUTY_STEPS;
}

static int apply_duty(const BoardPwmEntry *entry, uint8_t duty)
{
  int index = entry_index(entry);
  if (index < 0 || !s_pwm_state[index].configured || !*timer_configured_flag(entry)) {
    return -7;
  }

  TIM_HandleTypeDef *timer = timer_handle(entry);
  __HAL_TIM_SET_COMPARE(timer, entry->channel, duty_to_pulse(entry, duty));
  s_pwm_state[index].duty = duty;

  if (!s_pwm_state[index].running) {
    configure_pwm_pin(entry);
    if (HAL_TIM_PWM_Start(timer, entry->channel) != HAL_OK) {
      return -6;
    }
    s_pwm_state[index].running = 1u;
  }

  return 0;
}

static void restore_running_channels(const BoardPwmEntry *changed_entry)
{
  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (k_pwm_map[i].shared_group != changed_entry->shared_group) {
      continue;
    }
    if (s_pwm_state[i].configured) {
      __HAL_TIM_SET_COMPARE(timer_handle(&k_pwm_map[i]), k_pwm_map[i].channel,
                            duty_to_pulse(&k_pwm_map[i], s_pwm_state[i].duty));
    }
    if (s_pwm_state[i].running) {
      configure_pwm_pin(&k_pwm_map[i]);
      (void)HAL_TIM_PWM_Start(timer_handle(&k_pwm_map[i]), k_pwm_map[i].channel);
    }
  }
}

size_t Board_PWM_Count(void)
{
  size_t count = 0;
  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (!k_pwm_map[i].reserved) {
      ++count;
    }
  }
  return count;
}

const BoardPwmEntry *Board_PWM_At(size_t index)
{
  size_t visible_index = 0;
  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (k_pwm_map[i].reserved) {
      continue;
    }
    if (visible_index == index) {
      return &k_pwm_map[i];
    }
    ++visible_index;
  }
  return NULL;
}

const BoardPwmEntry *Board_PWM_FindById(uint8_t id)
{
  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (k_pwm_map[i].id == id && !k_pwm_map[i].reserved) {
      return &k_pwm_map[i];
    }
  }
  return NULL;
}

const BoardPwmEntry *Board_PWM_FindByName(const char *name)
{
  if (name == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < sizeof(k_pwm_map) / sizeof(k_pwm_map[0]); ++i) {
    if (!k_pwm_map[i].reserved && name_equals(k_pwm_map[i].name, name)) {
      return &k_pwm_map[i];
    }
  }

  if ((name[0] == 'D' || name[0] == 'd') && isdigit((unsigned char)name[1])) {
    char *end = NULL;
    unsigned long id = strtoul(&name[1], &end, 10);
    if (end != &name[1] && *end == '\0' && id <= UINT8_MAX) {
      return Board_PWM_FindById((uint8_t)id);
    }
  }

  return NULL;
}

int Board_PWM_Setup(const BoardPwmEntry *entry, const BoardPwmConfig *config)
{
  int index = entry_index(entry);
  if (index < 0 || config == NULL || entry->reserved) {
    return -1;
  }
  if (config->freq < entry->min_freq || config->freq > entry->max_freq) {
    return -5;
  }

  const GpioMapEntry *gpio = gpio_for_pwm(entry);
  if (gpio == NULL || (gpio->caps & BOARD_GPIO_CAP_PWM) == 0u) {
    return -3;
  }

  int owner_status = Board_GPIO_AcquireOwner(gpio, BOARD_PIN_OWNER_PWM);
  if (owner_status != 0) {
    return owner_status;
  }

  int status = 0;
  if (!*timer_configured_flag(entry) || *timer_frequency(entry) != config->freq) {
    /* Channels in the same shared group use one timer frequency. */
    status = timer_configure_frequency(entry, config->freq);
    if (status != 0) {
      Board_GPIO_ReleaseOwner(gpio, BOARD_PIN_OWNER_PWM);
      return status;
    }
    restore_running_channels(entry);
  }

  configure_pwm_pin(entry);
  s_pwm_state[index].configured = 1u;
  s_pwm_state[index].duty = config->duty;

  TIM_HandleTypeDef *timer = timer_handle(entry);
  __HAL_TIM_SET_COMPARE(timer, entry->channel, duty_to_pulse(entry, config->duty));

  if (config->start) {
    if (HAL_TIM_PWM_Start(timer, entry->channel) != HAL_OK) {
      return -6;
    }
    s_pwm_state[index].running = 1u;
  } else {
    (void)HAL_TIM_PWM_Stop(timer, entry->channel);
    s_pwm_state[index].running = 0u;
  }

  return 0;
}

int Board_PWM_Write(const BoardPwmEntry *entry, uint8_t duty)
{
  int index = entry_index(entry);
  if (index < 0) {
    return -1;
  }

  if (!s_pwm_state[index].configured) {
    BoardPwmConfig config = {
      .freq = BOARD_PWM_DEFAULT_FREQ,
      .duty = duty,
      .start = 1u
    };
    return Board_PWM_Setup(entry, &config);
  }

  return apply_duty(entry, duty);
}

int Board_PWM_Read(const BoardPwmEntry *entry, uint8_t *out_duty)
{
  int index = entry_index(entry);
  if (index < 0 || out_duty == NULL) {
    return -1;
  }
  if (!s_pwm_state[index].configured) {
    return -7;
  }

  *out_duty = s_pwm_state[index].duty;
  return 0;
}

int Board_PWM_SetFreq(const BoardPwmEntry *entry, uint32_t freq)
{
  int index = entry_index(entry);
  if (index < 0) {
    return -1;
  }

  BoardPwmConfig config = {
    .freq = freq,
    .duty = s_pwm_state[index].configured ? s_pwm_state[index].duty : 0u,
    .start = s_pwm_state[index].running
  };
  return Board_PWM_Setup(entry, &config);
}

int Board_PWM_GetFreq(const BoardPwmEntry *entry, uint32_t *out_freq)
{
  int index = entry_index(entry);
  if (index < 0 || out_freq == NULL) {
    return -1;
  }
  if (!s_pwm_state[index].configured || !*timer_configured_flag(entry)) {
    return -7;
  }

  *out_freq = *timer_frequency(entry);
  return 0;
}

int Board_PWM_Stop(const BoardPwmEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0) {
    return -1;
  }
  if (!s_pwm_state[index].configured) {
    return -7;
  }

  HAL_TIM_PWM_Stop(timer_handle(entry), entry->channel);
  s_pwm_state[index].running = 0u;
  return 0;
}

int Board_PWM_Release(const BoardPwmEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0) {
    return -1;
  }

  const GpioMapEntry *gpio = gpio_for_pwm(entry);
  if (gpio == NULL) {
    return -1;
  }

  (void)HAL_TIM_PWM_Stop(timer_handle(entry), entry->channel);
  s_pwm_state[index].configured = 0u;
  s_pwm_state[index].running = 0u;
  s_pwm_state[index].duty = 0u;

  GPIO_InitTypeDef init = {0};
  init.Pin = gpio->pin;
  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init((GPIO_TypeDef *)gpio->port, &init);

  Board_GPIO_ReleaseOwner(gpio, BOARD_PIN_OWNER_PWM);
  return 0;
}

int Board_PWM_GetState(const BoardPwmEntry *entry, BoardPwmState *out_state)
{
  int index = entry_index(entry);
  if (index < 0 || out_state == NULL) {
    return -1;
  }

  out_state->configured = s_pwm_state[index].configured;
  out_state->running = s_pwm_state[index].running;
  out_state->duty = s_pwm_state[index].duty;
  out_state->freq = *timer_configured_flag(entry) ? *timer_frequency(entry) : 0u;
  return 0;
}
