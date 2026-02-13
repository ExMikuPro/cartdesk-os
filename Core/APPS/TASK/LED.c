//
// Created by Hatsune Miku on 2026/2/13.
//

#include "Task.h"

uint32_t TaskTicks_LED = 0;
uint8_t LED_Flag = 0;

void Task_LED() {

  switch (LED_Flag) {
    case 0:
      if (TaskTicks_LED == 0) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        LED_Flag = 1;
        TaskTicks_LED = 1000;
      }
      break;
    case 1:
      if (TaskTicks_LED == 0) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        LED_Flag = 0;
        TaskTicks_LED = 1000;
      }
      break;

  }
}