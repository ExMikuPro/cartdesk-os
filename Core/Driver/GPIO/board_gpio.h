#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_GPIO_CAP_INPUT        (1u << 0)
#define BOARD_GPIO_CAP_OUTPUT       (1u << 1)
#define BOARD_GPIO_CAP_PULLUP       (1u << 2)
#define BOARD_GPIO_CAP_PULLDOWN     (1u << 3)
#define BOARD_GPIO_CAP_IRQ          (1u << 4)
#define BOARD_GPIO_CAP_ADC          (1u << 5)
#define BOARD_GPIO_CAP_PWM          (1u << 6)
#define BOARD_GPIO_CAP_OPEN_DRAIN   (1u << 7)

#define BOARD_GPIO_MODE_INPUT              0
#define BOARD_GPIO_MODE_INPUT_PULLUP       1
#define BOARD_GPIO_MODE_INPUT_PULLDOWN     2
#define BOARD_GPIO_MODE_OUTPUT             3
#define BOARD_GPIO_MODE_OUTPUT_OPEN_DRAIN  4
#define BOARD_GPIO_MODE_ANALOG             5

#define BOARD_GPIO_SPEED_LOW     0
#define BOARD_GPIO_SPEED_MEDIUM  1
#define BOARD_GPIO_SPEED_HIGH    2

typedef enum {
  BOARD_PIN_OWNER_NONE = 0,
  BOARD_PIN_OWNER_GPIO,
  BOARD_PIN_OWNER_PWM,
  BOARD_PIN_OWNER_ADC,
  BOARD_PIN_OWNER_SYSTEM
} BoardPinOwner;

typedef struct {
  uint8_t id;
  const char *name;
  void *port;
  uint32_t pin;
  uint32_t caps;
  uint8_t default_mode;
  uint8_t reserved;
} GpioMapEntry;

typedef struct {
  uint8_t mode;
  uint8_t speed;
  int8_t initial;
  int8_t pull;
} BoardGpioConfig;

size_t Board_GPIO_Count(void);
const GpioMapEntry *Board_GPIO_At(size_t index);
const GpioMapEntry *Board_GPIO_FindById(uint8_t id);
const GpioMapEntry *Board_GPIO_FindByName(const char *name);

int Board_GPIO_Setup(const GpioMapEntry *entry, const BoardGpioConfig *config);
int Board_GPIO_Read(const GpioMapEntry *entry);
int Board_GPIO_Write(const GpioMapEntry *entry, int level);
int Board_GPIO_Toggle(const GpioMapEntry *entry);
int Board_GPIO_Release(const GpioMapEntry *entry);

BoardPinOwner Board_GPIO_GetOwner(const GpioMapEntry *entry);
int Board_GPIO_AcquireOwner(const GpioMapEntry *entry, BoardPinOwner owner);
void Board_GPIO_ReleaseOwner(const GpioMapEntry *entry, BoardPinOwner owner);

#ifdef __cplusplus
}
#endif
