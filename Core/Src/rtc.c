#include "rtc.h"

RTC_HandleTypeDef hrtc;

__attribute__((used, noinline)) void MX_RTC_Init(void)
{
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 249;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK) {
    Error_Handler();
  }
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *rtcHandle)
{
  if (rtcHandle->Instance != RTC) {
    return;
  }

  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
  osc.LSIState = RCC_LSI_ON;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    Error_Handler();
  }

  HAL_PWR_EnableBkUpAccess();

  /*
   * HAL_RCCEx_PeriphCLKConfig resets the backup domain only when RTCSEL must
   * change. Once LSI is selected, normal software resets skip that operation.
   */
  if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_RTCCLKSOURCE_LSI) {
    RCC_PeriphCLKInitTypeDef periph_clk = {0};
    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph_clk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
      Error_Handler();
    }
  }

  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTC_CLK_ENABLE();
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef *rtcHandle)
{
  if (rtcHandle->Instance == RTC) {
    __HAL_RCC_RTC_CLK_DISABLE();
  }
}
