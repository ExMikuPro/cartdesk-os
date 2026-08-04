/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
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

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#if CARTDESK_USB_SD_MSC_ENABLE
#include "usbd_msc.h"
#include "usbd_storage_if.h"
#endif

/* USER CODE BEGIN Includes */

#include <string.h>

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

static bool s_sd_msc_mode;

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

static bool prv_start_usb_class(bool sd_msc_mode);

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceHS
    __attribute__((section(".usb_ram_data"), aligned(32)));

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

extern uint8_t __usb_ram_start__;
extern uint8_t __usb_ram_end__;

/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */

  memset(&__usb_ram_start__, 0,
         (size_t)(&__usb_ram_end__ - &__usb_ram_start__));

  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* Init Device Library, add supported class and start the library. */
  if (!prv_start_usb_class(false)) Error_Handler();

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */
  HAL_PWREx_EnableUSBVoltageDetector();

  /* USER CODE END USB_DEVICE_Init_PostTreatment */
}

static bool prv_start_usb_class(bool sd_msc_mode)
{
  memset(&__usb_ram_start__, 0,
         (size_t)(&__usb_ram_end__ - &__usb_ram_start__));

  if (USBD_Init(&hUsbDeviceHS, &HS_Desc, DEVICE_HS) != USBD_OK) return false;
#if CARTDESK_USB_SD_MSC_ENABLE
  if (sd_msc_mode) {
    if (USBD_RegisterClass(&hUsbDeviceHS, &USBD_MSC) != USBD_OK ||
        USBD_MSC_RegisterStorage(&hUsbDeviceHS,
                                 &USBD_Storage_Interface_fops_HS) != USBD_OK) {
      (void)USBD_DeInit(&hUsbDeviceHS);
      return false;
    }
  } else
#else
  (void)sd_msc_mode;
#endif
  {
    if (USBD_RegisterClass(&hUsbDeviceHS, &USBD_CDC) != USBD_OK ||
        USBD_CDC_RegisterInterface(&hUsbDeviceHS,
                                   &USBD_Interface_fops_HS) != USBD_OK) {
      (void)USBD_DeInit(&hUsbDeviceHS);
      return false;
    }
  }
  if (USBD_Start(&hUsbDeviceHS) != USBD_OK) {
    (void)USBD_DeInit(&hUsbDeviceHS);
    return false;
  }
  HAL_PWREx_EnableUSBVoltageDetector();
  s_sd_msc_mode = sd_msc_mode;
  return true;
}

bool USB_Device_SetSdMscMode(bool enabled)
{
#if CARTDESK_USB_SD_MSC_ENABLE
  bool previous_mode = s_sd_msc_mode;

  if (enabled == s_sd_msc_mode) return true;

  (void)USBD_Stop(&hUsbDeviceHS);
  (void)USBD_DeInit(&hUsbDeviceHS);
  HAL_Delay(250u);
  if (prv_start_usb_class(enabled)) return true;

  /* Keep the last usable USB personality if the requested class fails. */
  HAL_Delay(250u);
  (void)prv_start_usb_class(previous_mode);
  return false;
#else
  (void)enabled;
  return false;
#endif
}

bool USB_Device_IsSdMscMode(void)
{
  return s_sd_msc_mode;
}

/**
  * @}
  */

/**
  * @}
  */
