/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_task.h"
#include "audio_task.h"
#include "background_task.h"
#include "peripheral_task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for app */
osThreadId_t appHandle;
const osThreadAttr_t app_attributes = {
  .name = "app",
  .stack_size = 8192 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for audio */
osThreadId_t audioHandle;
const osThreadAttr_t audio_attributes = {
  .name = "audio",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for io */
osThreadId_t ioHandle;
const osThreadAttr_t io_attributes = {
  .name = "io",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for background */
osThreadId_t backgroundHandle;
const osThreadAttr_t background_attributes = {
  .name = "background",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartAppTask(void *argument);
void StartAudioTask(void *argument);
void StartIoTask(void *argument);
void StartBackgroundTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of app */
  appHandle = osThreadNew(StartAppTask, NULL, &app_attributes);

  /* creation of audio */
  audioHandle = osThreadNew(StartAudioTask, NULL, &audio_attributes);

  /* creation of io */
  ioHandle = osThreadNew(StartIoTask, NULL, &io_attributes);

  /* creation of background */
  backgroundHandle = osThreadNew(StartBackgroundTask, NULL, &background_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  if (appHandle == NULL || audioHandle == NULL || ioHandle == NULL ||
      backgroundHandle == NULL)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartAppTask */
/**
  * @brief  Function implementing the app thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartAppTask */
void StartAppTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartAppTask */
  CartdeskAppTask_Run(argument);
  osThreadExit();
  /* USER CODE END StartAppTask */
}

/* USER CODE BEGIN Header_StartAudioTask */
/**
* @brief Function implementing the audio thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument)
{
  /* USER CODE BEGIN StartAudioTask */
  CartdeskAudioTask_Run(argument);
  osThreadExit();
  /* USER CODE END StartAudioTask */
}

/* USER CODE BEGIN Header_StartIoTask */
/**
* @brief Function implementing the io thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartIoTask */
void StartIoTask(void *argument)
{
  /* USER CODE BEGIN StartIoTask */
  CartdeskPeripheralTask_Run(argument);
  osThreadExit();
  /* USER CODE END StartIoTask */
}

/* USER CODE BEGIN Header_StartBackgroundTask */
/**
* @brief Function implementing the background thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartBackgroundTask */
void StartBackgroundTask(void *argument)
{
  /* USER CODE BEGIN StartBackgroundTask */
  CartdeskBackgroundTask_Run(argument);
  osThreadExit();
  /* USER CODE END StartBackgroundTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

