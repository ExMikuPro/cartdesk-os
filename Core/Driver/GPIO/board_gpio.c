#include "board_gpio.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#define BOARD_GPIO_DEFAULT_CAPS \
  (BOARD_GPIO_CAP_INPUT | BOARD_GPIO_CAP_OUTPUT | BOARD_GPIO_CAP_PULLUP | \
   BOARD_GPIO_CAP_PULLDOWN | BOARD_GPIO_CAP_OPEN_DRAIN)

#define BOARD_GPIO_TIM3_CAPS (BOARD_GPIO_DEFAULT_CAPS | BOARD_GPIO_CAP_PWM)

static const GpioMapEntry k_gpio_map[] = {
  {0, "GPIO0", GPIOA, GPIO_PIN_6, BOARD_GPIO_TIM3_CAPS, BOARD_GPIO_MODE_INPUT, 0},
  {1, "GPIO1", GPIOA, GPIO_PIN_7, BOARD_GPIO_TIM3_CAPS, BOARD_GPIO_MODE_INPUT, 0},
  {2, "GPIO2", GPIOB, GPIO_PIN_0, BOARD_GPIO_TIM3_CAPS, BOARD_GPIO_MODE_INPUT, 0},
  {3, "GPIO3", GPIOB, GPIO_PIN_1, BOARD_GPIO_TIM3_CAPS, BOARD_GPIO_MODE_INPUT, 0},
  {4, "GPIO4", GPIOA, GPIO_PIN_3, BOARD_GPIO_TIM3_CAPS, BOARD_GPIO_MODE_INPUT, 0},
};

typedef struct {
  uint8_t configured;
  uint8_t mode;
} GpioPinState;

static BoardPinOwner s_gpio_owners[sizeof(k_gpio_map) / sizeof(k_gpio_map[0])];
static GpioPinState s_gpio_state[sizeof(k_gpio_map) / sizeof(k_gpio_map[0])];

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

static void enable_gpio_clock(const GpioMapEntry *entry)
{
  void *port = entry ? entry->port : NULL;

  if (port == GPIOA) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
  } else if (port == GPIOB) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
  } else if (port == GPIOC) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
  } else if (port == GPIOD) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
  } else if (port == GPIOE) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
  } else if (port == GPIOF) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
  } else if (port == GPIOG) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
  } else if (port == GPIOH) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
#ifdef GPIOI
  } else if (port == GPIOI) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
#endif
#ifdef GPIOJ
  } else if (port == GPIOJ) {
    __HAL_RCC_GPIOJ_CLK_ENABLE();
#endif
#ifdef GPIOK
  } else if (port == GPIOK) {
    __HAL_RCC_GPIOK_CLK_ENABLE();
#endif
  }
}

static uint32_t speed_to_hal(uint8_t speed)
{
  switch (speed) {
    case BOARD_GPIO_SPEED_MEDIUM:
      return GPIO_SPEED_FREQ_MEDIUM;
    case BOARD_GPIO_SPEED_HIGH:
      return GPIO_SPEED_FREQ_HIGH;
    case BOARD_GPIO_SPEED_LOW:
    default:
      return GPIO_SPEED_FREQ_LOW;
  }
}

static uint32_t mode_to_hal(uint8_t mode)
{
  switch (mode) {
    case BOARD_GPIO_MODE_INPUT:
    case BOARD_GPIO_MODE_INPUT_PULLUP:
    case BOARD_GPIO_MODE_INPUT_PULLDOWN:
      return GPIO_MODE_INPUT;
    case BOARD_GPIO_MODE_OUTPUT:
      return GPIO_MODE_OUTPUT_PP;
    case BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN:
      return GPIO_MODE_OUTPUT_OD;
    case BOARD_GPIO_MODE_ANALOG:
      return GPIO_MODE_ANALOG;
    default:
      return UINT32_MAX;
  }
}

static uint32_t pull_to_hal(uint8_t mode, int8_t pull)
{
  if (pull >= 0) {
    switch (pull) {
      case BOARD_GPIO_MODE_INPUT_PULLUP:
        return GPIO_PULLUP;
      case BOARD_GPIO_MODE_INPUT_PULLDOWN:
        return GPIO_PULLDOWN;
      default:
        return GPIO_NOPULL;
    }
  }

  switch (mode) {
    case BOARD_GPIO_MODE_INPUT_PULLUP:
      return GPIO_PULLUP;
    case BOARD_GPIO_MODE_INPUT_PULLDOWN:
      return GPIO_PULLDOWN;
    default:
      return GPIO_NOPULL;
  }
}

static int has_caps_for_mode(const GpioMapEntry *entry, uint8_t mode)
{
  if (entry == NULL || entry->reserved) {
    return 0;
  }

  switch (mode) {
    case BOARD_GPIO_MODE_INPUT:
      return (entry->caps & BOARD_GPIO_CAP_INPUT) != 0;
    case BOARD_GPIO_MODE_INPUT_PULLUP:
      return (entry->caps & (BOARD_GPIO_CAP_INPUT | BOARD_GPIO_CAP_PULLUP)) ==
             (BOARD_GPIO_CAP_INPUT | BOARD_GPIO_CAP_PULLUP);
    case BOARD_GPIO_MODE_INPUT_PULLDOWN:
      return (entry->caps & (BOARD_GPIO_CAP_INPUT | BOARD_GPIO_CAP_PULLDOWN)) ==
             (BOARD_GPIO_CAP_INPUT | BOARD_GPIO_CAP_PULLDOWN);
    case BOARD_GPIO_MODE_OUTPUT:
      return (entry->caps & BOARD_GPIO_CAP_OUTPUT) != 0;
    case BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN:
      return (entry->caps & (BOARD_GPIO_CAP_OUTPUT | BOARD_GPIO_CAP_OPEN_DRAIN)) ==
             (BOARD_GPIO_CAP_OUTPUT | BOARD_GPIO_CAP_OPEN_DRAIN);
    case BOARD_GPIO_MODE_ANALOG:
      return 1;
    default:
      return 0;
  }
}

static int entry_index(const GpioMapEntry *entry)
{
  if (entry == NULL) {
    return -1;
  }

  for (size_t i = 0; i < sizeof(k_gpio_map) / sizeof(k_gpio_map[0]); ++i) {
    if (entry == &k_gpio_map[i]) {
      return (int)i;
    }
  }

  return -1;
}

BoardPinOwner Board_GPIO_GetOwner(const GpioMapEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0) {
    return BOARD_PIN_OWNER_SYSTEM;
  }
  return s_gpio_owners[index];
}

int Board_GPIO_AcquireOwner(const GpioMapEntry *entry, BoardPinOwner owner)
{
  int index = entry_index(entry);
  if (index < 0 || owner == BOARD_PIN_OWNER_NONE) {
    return -1;
  }
  if (entry->reserved) {
    return -2;
  }
  if (s_gpio_owners[index] != BOARD_PIN_OWNER_NONE && s_gpio_owners[index] != owner) {
    return -4;
  }

  s_gpio_owners[index] = owner;
  return 0;
}

void Board_GPIO_ReleaseOwner(const GpioMapEntry *entry, BoardPinOwner owner)
{
  int index = entry_index(entry);
  if (index < 0) {
    return;
  }
  if (s_gpio_owners[index] == owner) {
    s_gpio_owners[index] = BOARD_PIN_OWNER_NONE;
  }
}

size_t Board_GPIO_Count(void)
{
  size_t count = 0;
  for (size_t i = 0; i < sizeof(k_gpio_map) / sizeof(k_gpio_map[0]); ++i) {
    if (!k_gpio_map[i].reserved) {
      ++count;
    }
  }
  return count;
}

const GpioMapEntry *Board_GPIO_At(size_t index)
{
  size_t visible_index = 0;
  for (size_t i = 0; i < sizeof(k_gpio_map) / sizeof(k_gpio_map[0]); ++i) {
    if (k_gpio_map[i].reserved) {
      continue;
    }
    if (visible_index == index) {
      return &k_gpio_map[i];
    }
    ++visible_index;
  }
  return NULL;
}

const GpioMapEntry *Board_GPIO_FindById(uint8_t id)
{
  for (size_t i = 0; i < sizeof(k_gpio_map) / sizeof(k_gpio_map[0]); ++i) {
    if (k_gpio_map[i].id == id && !k_gpio_map[i].reserved) {
      return &k_gpio_map[i];
    }
  }
  return NULL;
}

const GpioMapEntry *Board_GPIO_FindByName(const char *name)
{
  if (name == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < sizeof(k_gpio_map) / sizeof(k_gpio_map[0]); ++i) {
    if (!k_gpio_map[i].reserved && name_equals(k_gpio_map[i].name, name)) {
      return &k_gpio_map[i];
    }
  }

  if ((name[0] == 'D' || name[0] == 'd') && isdigit((unsigned char)name[1])) {
    char *end = NULL;
    unsigned long id = strtoul(&name[1], &end, 10);
    if (end != &name[1] && *end == '\0' && id <= UINT8_MAX) {
      return Board_GPIO_FindById((uint8_t)id);
    }
  }

  return NULL;
}

int Board_GPIO_Setup(const GpioMapEntry *entry, const BoardGpioConfig *config)
{
  int index = entry_index(entry);
  if (index < 0 || config == NULL || entry->port == NULL || entry->pin == 0u) {
    return -1;
  }
  if (!has_caps_for_mode(entry, config->mode)) {
    return -2;
  }
  int owner_status = Board_GPIO_AcquireOwner(entry, BOARD_PIN_OWNER_GPIO);
  if (owner_status != 0) {
    return owner_status;
  }

  enable_gpio_clock(entry);

  if (config->initial >= 0 &&
      (config->mode == BOARD_GPIO_MODE_OUTPUT ||
       config->mode == BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN)) {
    HAL_GPIO_WritePin((GPIO_TypeDef *)entry->port, (uint16_t)entry->pin,
                      config->initial ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }

  GPIO_InitTypeDef init = {0};
  init.Pin = entry->pin;
  init.Mode = mode_to_hal(config->mode);
  if (init.Mode == UINT32_MAX) {
    return -3;
  }
  init.Pull = pull_to_hal(config->mode, config->pull);
  init.Speed = speed_to_hal(config->speed);

  HAL_GPIO_Init((GPIO_TypeDef *)entry->port, &init);
  s_gpio_state[index].configured = 1u;
  s_gpio_state[index].mode = config->mode;
  return 0;
}

int Board_GPIO_Read(const GpioMapEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0 || entry->port == NULL || entry->pin == 0u || entry->reserved) {
    return -1;
  }
  if (!s_gpio_state[index].configured ||
      s_gpio_owners[index] != BOARD_PIN_OWNER_GPIO) {
    return -5;
  }

  enable_gpio_clock(entry);
  return HAL_GPIO_ReadPin((GPIO_TypeDef *)entry->port, (uint16_t)entry->pin) == GPIO_PIN_SET ? 1 : 0;
}

int Board_GPIO_Write(const GpioMapEntry *entry, int level)
{
  int index = entry_index(entry);
  if (index < 0 || entry->port == NULL || entry->pin == 0u || entry->reserved) {
    return -1;
  }
  if ((entry->caps & BOARD_GPIO_CAP_OUTPUT) == 0u) {
    return -2;
  }
  if (!s_gpio_state[index].configured ||
      s_gpio_owners[index] != BOARD_PIN_OWNER_GPIO) {
    return -5;
  }
  if (s_gpio_state[index].mode != BOARD_GPIO_MODE_OUTPUT &&
      s_gpio_state[index].mode != BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN) {
    return -6;
  }

  enable_gpio_clock(entry);
  HAL_GPIO_WritePin((GPIO_TypeDef *)entry->port, (uint16_t)entry->pin,
                    level ? GPIO_PIN_SET : GPIO_PIN_RESET);
  return 0;
}

int Board_GPIO_Toggle(const GpioMapEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0 || entry->port == NULL || entry->pin == 0u || entry->reserved) {
    return -1;
  }
  if ((entry->caps & BOARD_GPIO_CAP_OUTPUT) == 0u) {
    return -2;
  }
  if (!s_gpio_state[index].configured ||
      s_gpio_owners[index] != BOARD_PIN_OWNER_GPIO) {
    return -5;
  }
  if (s_gpio_state[index].mode != BOARD_GPIO_MODE_OUTPUT &&
      s_gpio_state[index].mode != BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN) {
    return -6;
  }

  enable_gpio_clock(entry);
  HAL_GPIO_TogglePin((GPIO_TypeDef *)entry->port, (uint16_t)entry->pin);
  return 0;
}

int Board_GPIO_Release(const GpioMapEntry *entry)
{
  int index = entry_index(entry);
  if (index < 0 || entry->port == NULL || entry->pin == 0u || entry->reserved) {
    return -1;
  }
  if (Board_GPIO_GetOwner(entry) == BOARD_PIN_OWNER_PWM) {
    return -4;
  }

  enable_gpio_clock(entry);

  GPIO_InitTypeDef init = {0};
  init.Pin = entry->pin;
  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init((GPIO_TypeDef *)entry->port, &init);
  s_gpio_state[index].configured = 0u;
  s_gpio_state[index].mode = BOARD_GPIO_MODE_ANALOG;
  Board_GPIO_ReleaseOwner(entry, BOARD_PIN_OWNER_GPIO);
  return 0;
}
