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
#include "tim.h"
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

/* Storage: QSPI NOR + littlefs */
#include "lua_vm.h"
#include "../Driver/TOUCH/touch.h"
#include "Task.h"
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

#define GT_ADDR7   0x5D              // 不通就换 0x14
#define GT_ADDR    (GT_ADDR7 << 1)   // HAL 需要左移1位

static void GT_TestRead_814D(void) {
  HAL_StatusTypeDef st;
  uint8_t n = 0;

  // // 先确认设备在线
  // st = HAL_I2C_IsDeviceReady(&hi2c1, GT_ADDR, 3, 1000);
  // if (st != HAL_OK) {
  //   __BKPT(0); // GDB看 st/hi2c1.ErrorCode
  //   return;
  // }

  // 读 0x814D
  st = HAL_I2C_Mem_Read(&hi2c1, GT_ADDR,
                        0x814D, I2C_MEMADD_SIZE_16BIT,
                        &n, 1, 100);

  __BKPT(0); // 在这里用 GDB 看 n 和 st
}

static inline uint16_t tim17_us_now(void) {
  return (uint16_t) __HAL_TIM_GET_COUNTER(&htim17);
}

void delay_us_tim17(uint16_t us) {
  uint16_t start = tim17_us_now();
  while ((uint16_t) (tim17_us_now() - start) < us) {
  }
}

/**
  * @brief EXTI外部中断回调函数
  * @param GPIO_Pin 触发中断的引脚
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == TOUCH_INT_Pin) {
    Touch_IRQHandler();
  }
}

#define TEST_OFF   (0x00000000u)   // 更保守：最后 64KB 的起始（对齐）
#define TEST_LEN   (256u)

static uint8_t tx[TEST_LEN];
static uint8_t rx[TEST_LEN];

void flash_write_smoke(void)
{
  for (uint32_t i = 0; i < TEST_LEN; i++) tx[i] = (uint8_t)(i ^ 0xA5);

  // 1) 退出映射（写/擦前必须）
  (void)FLASH_DisableMemoryMapped(&g_flash);

  // 2) 先读一次擦前内容，确认当前区域状态
  memset(rx, 0, TEST_LEN);
  FLASH_Status st = FLASH_Read(&g_flash, TEST_OFF, rx, TEST_LEN);
  if (st != FLASH_OK) goto fail;
  printf("[FLASH] before erase [0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7]);

  // 3) 擦 4KB 扇区（地址必须 4K 对齐）
  st = FLASH_Erase4K(&g_flash, TEST_OFF);
  if (st != FLASH_OK) goto fail;

  // 4) 擦后读回，确认全 0xFF
  memset(rx, 0, TEST_LEN);
  st = FLASH_Read(&g_flash, TEST_OFF, rx, TEST_LEN);
  if (st != FLASH_OK) goto fail;
  {
    bool all_ff = true;
    for (uint32_t i = 0; i < TEST_LEN; i++) if (rx[i] != 0xFF) { all_ff = false; break; }
    printf("[FLASH] after erase: %s (rx[0]=%02X)\r\n", all_ff ? "all 0xFF OK" : "ERASE FAIL!", rx[0]);
    if (!all_ff) goto fail; // 擦除本身有问题，停在这里
  }

  // 5) 写入 256B（刚好一页）
  st = FLASH_Prog(&g_flash, TEST_OFF, tx, TEST_LEN);
  if (st != FLASH_OK) goto fail;

  // 6) 单线读回比对（READ 0x03，0 dummy，最可靠，排除 dummy cycles 干扰）
  memset(rx, 0, TEST_LEN);
  st = FLASH_Read(&g_flash, TEST_OFF, rx, TEST_LEN);
  if (st != FLASH_OK) goto fail;

  if (memcmp(tx, rx, TEST_LEN) == 0) {
    printf("[FLASH] single-line read: PASS\r\n");
    // goto done;
  } else {
    printf("[FLASH] single-line read: FAIL — first diff: tx[i]=%02X rx[i]=%02X\r\n",
           tx[0], rx[0]);
    // 打印前 16 字节帮助对比
    printf("[FLASH]   tx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           tx[0],tx[1],tx[2],tx[3],tx[4],tx[5],tx[6],tx[7]);
    printf("[FLASH]   rx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7]);
    goto fail;
  }

  // 7) 四线快速读回比对（测试 dummy cycles 是否正确）
  memset(rx, 0, TEST_LEN);
  st = FLASH_ReadFastQuad(&g_flash, TEST_OFF, rx, TEST_LEN);
  if (st != FLASH_OK) goto fail;

  if (memcmp(tx, rx, TEST_LEN) == 0) {
    printf("[FLASH] quad fast read: PASS\r\n");
    printf("[FLASH] write smoke ALL OK\r\n");
  } else {
    printf("[FLASH] quad fast read: FAIL (dummy cycles or AlternateBytes issue)\r\n");
    printf("[FLASH]   tx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           tx[0],tx[1],tx[2],tx[3],tx[4],tx[5],tx[6],tx[7]);
    printf("[FLASH]   rx[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7]);
    printf("[FLASH]   --> 请修改 flash.c FLASH_ReadFastQuad:\r\n");
    printf("[FLASH]       加 AlternateByteMode=4_LINES, AlternateBytesSize=8_BITS, AlternateBytes=0x00\r\n");
    printf("[FLASH]       DummyCycles 改为 4\r\n");
    goto fail;
  }

  done:
  // 8) 写完重新开映射
  (void)FLASH_EnableMemoryMapped(&g_flash);
  return;

  fail:
  {
    const FLASH_ErrorInfo *e = FLASH_LastError(&g_flash);
    if (e) printf("[FLASH] HAL fail code=%u step=%s line=%lu addr=0x%08lX\r\n",
                  (unsigned)e->code, e->step ? e->step : "?",
                  (unsigned long)e->line, (unsigned long)e->addr);
  }
  (void)FLASH_EnableMemoryMapped(&g_flash);
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
   // SCB_EnableDCache();

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
  MX_I2C2_Init();
  MX_TIM17_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */
  /* 初始化 SDRAM */
  SDRAM_Init();


  /* 初始化 QSPI NOR + littlefs */
  Storage_InitOrDie();
  HAL_TIM_Base_Start(&htim17);


  uint32_t jedec = 0;
  FLASH_Status st = FLASH_ReadJEDEC(&g_flash, &jedec);
  printf("[FLASH] JEDEC = 0x%06lX (期望 Winbond W25Q256: 0xEF4019)\r\n", (unsigned long)jedec);
  while (st != FLASH_OK) {}

  // 注意：FLASH_ReadJEDEC() 走命令模式，会自动退出映射
  // 所以要重新打开映射
  (void)FLASH_EnableMemoryMapped(&g_flash);

  volatile const uint8_t *mm = (volatile const uint8_t *)FLASH_MM_BASE;
  printf("[FLASH] MM[0..15] =");
  for (int i = 0; i < 16; i++) printf(" %02X", mm[i]);
  printf("\r\n");

  flash_write_smoke();

  HAL_TIM_Base_Start_IT(&htim16);
  /* LCD/UI */
  // ui_test_touch_drag_start();
  LCD_DisplayON();

  // DesignLauncher_Create(NULL); // 注释掉的启动器创建函数

  // Launcher_Init();
  // DesignLauncher_Create(lv_display_get_default());

  // lua_init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    // uint32_t now = HAL_GetTick();
    // if ((uint32_t)(now - t_lvgl) >= 5) {
    //   // 最多追赶一次，避免 while 补帧
    //   t_lvgl = now;
    //   lv_timer_handler();
    // }

    // Task_LED();
    // Task_LVGL();
    Task_LUA();

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

  /**
   * Region 5: SDRAM 帧缓冲区 0xD0000000, 8MB
   * LTDC 是 DMA，直接读物理内存，不经过 CPU Cache。
   * 必须配成 Non-Cacheable，否则 CPU 写进 Cache 后
   * LTDC 读到的还是旧数据，导致画面撕裂/花屏。
   * Write-Buffer 开启让 CPU 写操作不阻塞。
   */
  MPU_InitStruct.Number = MPU_REGION_NUMBER5;
  MPU_InitStruct.BaseAddress = 0xD0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;       /* 覆盖三个帧缓冲共 4.38MB，取 8MB */
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;  /* ← 关键：关 Cache */
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;    /* Write-Buffer 加速 CPU 写 */
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /**
   * Region 9: SDRAM 图片/Heap 区 0xD0465000, 剩余空间
   * DMA2D 搬运完成后 CPU 只读，配 Write-Through。
   * 比 Non-Cacheable 快，且不会有 CPU 写脏数据的问题。
   */
  MPU_InitStruct.Number = MPU_REGION_NUMBER9;
  MPU_InitStruct.BaseAddress = 0xD0400000;         /* 对齐到 4MB 边界 */
  MPU_InitStruct.Size = MPU_REGION_SIZE_64MB;      /* 覆盖剩余 SDRAM */
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE; /* Write-Through，不写分配 */
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;

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