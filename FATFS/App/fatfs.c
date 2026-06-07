/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
static uint8_t s_sd_fatfs_mounted = 0;

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  s_sd_fatfs_mounted = 0;
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  return 0;
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */
FRESULT SD_FATFS_Mount(void)
{
  if (retSD != 0U) {
    return FR_INVALID_DRIVE;
  }

  if (s_sd_fatfs_mounted) {
    return FR_OK;
  }

  const char *path = (SDPath[0] != '\0') ? SDPath : "0:";
  FRESULT fr = f_mount(&SDFatFS, path, 1);
  s_sd_fatfs_mounted = (fr == FR_OK) ? 1U : 0U;
  return fr;
}

void SD_FATFS_InvalidateMount(void)
{
  s_sd_fatfs_mounted = 0;
}

/* USER CODE END Application */
