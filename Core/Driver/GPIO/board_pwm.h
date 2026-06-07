#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_PWM_MIN_DUTY       0u
#define BOARD_PWM_MAX_DUTY       255u
#define BOARD_PWM_DEFAULT_FREQ   1000u

typedef struct {
  uint8_t id;
  const char *name;
  void *timer;
  uint32_t channel;
  uint32_t min_freq;
  uint32_t max_freq;
  uint8_t duty_bits;
  uint8_t shared_group;
  uint8_t reserved;
} BoardPwmEntry;

typedef struct {
  uint32_t freq;
  uint8_t duty;
  uint8_t start;
} BoardPwmConfig;

typedef struct {
  uint8_t configured;
  uint8_t running;
  uint8_t duty;
  uint32_t freq;
} BoardPwmState;

size_t Board_PWM_Count(void);
const BoardPwmEntry *Board_PWM_At(size_t index);
const BoardPwmEntry *Board_PWM_FindById(uint8_t id);
const BoardPwmEntry *Board_PWM_FindByName(const char *name);

int Board_PWM_Setup(const BoardPwmEntry *entry, const BoardPwmConfig *config);
int Board_PWM_Write(const BoardPwmEntry *entry, uint8_t duty);
int Board_PWM_Read(const BoardPwmEntry *entry, uint8_t *out_duty);
int Board_PWM_SetFreq(const BoardPwmEntry *entry, uint32_t freq);
int Board_PWM_GetFreq(const BoardPwmEntry *entry, uint32_t *out_freq);
int Board_PWM_Stop(const BoardPwmEntry *entry);
int Board_PWM_Release(const BoardPwmEntry *entry);
int Board_PWM_GetState(const BoardPwmEntry *entry, BoardPwmState *out_state);

#ifdef __cplusplus
}
#endif
