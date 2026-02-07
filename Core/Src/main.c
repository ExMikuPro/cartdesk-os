/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "crc.h"
#include "dma2d.h"
#include "fatfs.h"
#include "i2c.h"
#include "ltdc.h"
#include "mdma.h"
#include "quadspi.h"
#include "rng.h"
#include "sdmmc.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../Driver/LCD/lcd.h"
#include "../Driver/SDRAM/sdram.h"
#include "Core/Screen/Page/ui_screen_launcher.h"


#include "demos/lv_demos.h"

/* Storage: QSPI NOR + littlefs */
#include "Core/APPS/LVGL/port/lvgl_init.h"
#include "Core/APPS/LVGL/src/core/lv_obj_pos.h"
#include "Core/APPS/LVGL/src/widgets/label/lv_label.h"
#include "EEPROM/eeprom.h"
#include "FLASH/flash.h"
#include "FLASH/lfs_port.h"
#include "RNG/rng_port.h"
#include "UID/uid.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

static FLASH_Handle g_flash;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* (user defines) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */


/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* (user private variables) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ============================== Storage init ============================== */

static void Storage_InitOrDie(void) {
  /* 1) 绑定 QSPI 句柄（不在这里初始化 QSPI 外设） */
  if (FLASH_Open(&g_flash, &hqspi, 64u * 1024u * 1024u) != FLASH_OK) {
    const FLASH_ErrorInfo *e = FLASH_LastError(&g_flash);
    printf("FLASH_Open fail step=%s line=%lu code=%d hal=%d qspi=0x%08lx\r\n",
           e ? e->step : "?", e ? (unsigned long) e->line : 0ul,
           e ? e->code : -1, e ? e->hal : -1, e ? (unsigned long) e->qspi_error : 0ul);
    Error_Handler();
  }

  /* 2) 芯片 bring-up：reset/QE/4-byte 等（不是 QSPI 外设 init） */
  if (FLASH_BringUp(&g_flash) != FLASH_OK) {
    const FLASH_ErrorInfo *e = FLASH_LastError(&g_flash);
    printf("FLASH_BringUp fail step=%s line=%lu code=%d hal=%d qspi=0x%08lx\r\n",
           e ? e->step : "?", e ? (unsigned long) e->line : 0ul,
           e ? e->code : -1, e ? e->hal : -1, e ? (unsigned long) e->qspi_error : 0ul);
    Error_Handler();
  }

  /* 3) 读取 JEDEC（可选，但很有用） */
  uint32_t jedec_id = 0;
  if (FLASH_ReadJEDEC(&g_flash, &jedec_id) == FLASH_OK) {
    printf("Flash JEDEC: 0x%06lX\r\n", (unsigned long) jedec_id);
  }

  /* 4) littlefs 绑定 & 挂载 */
  if (LFS_PortBind(&g_flash) != 0) {
    printf("LFS_PortBind fail\r\n");
    Error_Handler();
  }

  int lfs_err = LFS_MountOrFormat();
  if (lfs_err != 0) {
    printf("LFS_MountOrFormat err=%d\r\n", lfs_err);
    Error_Handler();
  }

  /* 如果你启用了 memory-mapped 读（XIP），可以在此打开：
     注意：建议 MPU 把 littlefs 分区设为 Non-Cacheable，避免写后读到旧数据。 */
  // (void)LFS_EnableMappedRead(1);
}

static lv_obj_t *g_box = NULL;

static void box_anim_x(void *obj, int32_t v)
{
  lv_obj_set_x((lv_obj_t *)obj, v);
}

void ui_test_moving_box_start(void)
{
  // 清空屏幕（可选，但建议排障时干净一点）
  lv_obj_clean(lv_screen_active());

  // 背景设为不透明纯黑（避免透明叠加引起“看起来像重影”）
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);

  // 创建小方块
  const int32_t box_w = 40;
  const int32_t box_h = 40;

  g_box = lv_obj_create(lv_screen_active());
  lv_obj_set_size(g_box, box_w, box_h);
  lv_obj_set_style_bg_opa(g_box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g_box, lv_color_hex(0x00FF00), 0); // 亮绿色方便观察
  lv_obj_set_style_border_width(g_box, 0, 0);
  lv_obj_set_style_radius(g_box, 0, 0);

  // 初始位置：垂直居中，x=0
  lv_obj_set_y(g_box, (LCD_H - box_h) / 2);
  lv_obj_set_x(g_box, 0);

  // 做左右往返动画
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_box);
  lv_anim_set_exec_cb(&a, box_anim_x);
  lv_anim_set_values(&a, 0, LCD_W - box_w);

  // 速度：这里 1000ms 从左到右；再 1000ms 从右到左
  lv_anim_set_time(&a, 1000);
  lv_anim_set_playback_time(&a, 1000);

  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_MDMA_Init();
  MX_LTDC_Init();
  MX_FMC_Init();
  MX_USART1_UART_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  MX_CRC_Init();
  MX_DMA2D_Init();
  MX_QUADSPI_Init();
  MX_I2C1_Init();
  MX_RNG_Init();
  /* USER CODE BEGIN 2 */
  /* 初始化 SDRAM */
  SDRAM_Init();
  /* 初始化 QSPI NOR + littlefs */
  Storage_InitOrDie();
  /* LCD/UI */
  // LCD_DoubleBufferInit();

  lvgl_init();
  ui_test_moving_box_start();
  LCD_DisplayON();
  // Launcher_Init();
  // LauncherLVGL_Create(lv_display_get_default());
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    lvgl_task_handler();
    HAL_Delay(5);  // 5ms调用一次
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 24;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCC_EnableCSS();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x20000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x30000000;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0x38000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER4;
  MPU_InitStruct.BaseAddress = 0x80000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER5;
  MPU_InitStruct.BaseAddress = 0xD0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER6;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER7;
  MPU_InitStruct.BaseAddress = 0x91000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_16MB;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER8;
  MPU_InitStruct.BaseAddress = 0x92000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
