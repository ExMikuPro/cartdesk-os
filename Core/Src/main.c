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
#include "ltdc.h"
#include "mdma.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

#include "sdram.h"
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

/* USER CODE BEGIN PV */


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


static volatile uint32_t s_mdma_done = 0;
static void _mdma_cplt_cb(MDMA_HandleTypeDef *h){ (void)h; s_mdma_done = 1; }

// 你在 GDB 里看这两个全局变量
volatile uint32_t g_mdma_cycles = 0;
volatile uint32_t g_mdma_mbps   = 0;

uint32_t MDMA_Test_SDRAM_to_SDRAM_MBps(void)
{
  const uint32_t sdram_base = 0xC0000000u;
  const uint32_t test_bytes = 4u * 1024u * 1024u;   // 4MB
  const uint32_t offset     = 8u * 1024u * 1024u;   // dst = base + 8MB
  const uint32_t words      = test_bytes / 4u;

  uint32_t *src = (uint32_t *)(sdram_base);
  uint32_t *dst = (uint32_t *)(sdram_base + offset);

  // 先确认 SystemCoreClock 非 0（避免除 0）
  if (SystemCoreClock == 0u) { g_mdma_cycles = 0; g_mdma_mbps = 0; return 0; }

  // 启动 DWT（如果不可用也没关系：我们会检测 cycles==0）
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  // 注册回调并启动
  s_mdma_done = 0;
  HAL_MDMA_RegisterCallback(&hmdma_mdma_channel0_sw_0,
                            HAL_MDMA_XFER_CPLT_CB_ID,
                            _mdma_cplt_cb);

  uint32_t t0 = DWT->CYCCNT;

  if (HAL_MDMA_Start_IT(&hmdma_mdma_channel0_sw_0,
                        (uint32_t)src,
                        (uint32_t)dst,
                        words,
                        1) != HAL_OK)
  {
    g_mdma_cycles = 0;
    g_mdma_mbps   = 0;
    return 0;
  }

  while (!s_mdma_done) {}

  uint32_t t1 = DWT->CYCCNT;
  uint32_t cycles = t1 - t0;
  g_mdma_cycles = cycles;

  // cycles==0 就直接返回 0，绝不做除法
  if (cycles == 0u) { g_mdma_mbps = 0; return 0; }

  // MB/s = bytes * CPU_Hz / cycles / 1e6
  uint64_t num = (uint64_t)test_bytes * (uint64_t)SystemCoreClock;
  uint64_t den = (uint64_t)cycles * 1000000ull;
  if (den == 0ull) { g_mdma_mbps = 0; return 0; } // 双保险

  g_mdma_mbps = (uint32_t)(num / den);
  return g_mdma_mbps;
}


volatile HAL_StatusTypeDef g_mdma_start_ret;
volatile HAL_MDMA_StateTypeDef g_mdma_state;
volatile uint32_t g_mdma_error;

uint32_t mdma_copy_poll(uint32_t dst, uint32_t src, uint32_t words, uint32_t timeout_ms)
{
  g_mdma_start_ret = HAL_MDMA_Start(&hmdma_mdma_channel0_sw_0, src, dst, words, 1);
  if (g_mdma_start_ret != HAL_OK) {
    g_mdma_state = HAL_MDMA_GetState(&hmdma_mdma_channel0_sw_0);
    g_mdma_error = HAL_MDMA_GetError(&hmdma_mdma_channel0_sw_0);
    return 0;
  }

  uint32_t t0 = HAL_GetTick();
  while (1)
  {
    g_mdma_state = HAL_MDMA_GetState(&hmdma_mdma_channel0_sw_0);
    g_mdma_error = HAL_MDMA_GetError(&hmdma_mdma_channel0_sw_0);

    if (g_mdma_state == HAL_MDMA_STATE_READY) return 1;
    if ((HAL_GetTick() - t0) > timeout_ms) return 0;
  }
}

#define BUFFER_SIZE              32

static const uint32_t SRC_Const_Buffer[BUFFER_SIZE] =
{
  0x01020304, 0x05060708, 0x090A0B0C, 0x0D0E0F10,
  0x11121314, 0x15161718, 0x191A1B1C, 0x1D1E1F20,
  0x21222324, 0x25262728, 0x292A2B2C, 0x2D2E2F30,
  0x31323334, 0x35363738, 0x393A3B3C, 0x3D3E3F40,
  0x41424344, 0x45464748, 0x494A4B4C, 0x4D4E4F50,
  0x51525354, 0x55565758, 0x595A5B5C, 0x5D5E5F60,
  0x61626364, 0x65666768, 0x696A6B6C, 0x6D6E6F70,
  0x71727374, 0x75767778, 0x797A7B7C, 0x7D7E7F80
};

__attribute__((at(0x24004000))) uint32_t DESTBuffer[BUFFER_SIZE];




uint8_t Buffercmp(const uint32_t* p0, const uint32_t* p1, uint32_t words)
{
  while (words--)
  {
    if (*p0++ != *p1++) return 0;
  }
  return 1;
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
  /* USER CODE BEGIN 2 */

  __HAL_RCC_LTDC_CLK_DISABLE();   // 直接关 LTDC 时钟

  SDRAM_Init();

  HAL_StatusTypeDef DMA_Status = HAL_ERROR;


  // volatile uint32_t *s = (uint32_t*)0xD0000000;  // 你实际 SDRAM bank 地址
  // s[0] = 0x11223344;
  // s[1] = 0xA5A5A5A5;
  // volatile uint32_t a = s[0];
  // volatile uint32_t b = s[1];
  // (void)a; (void)b;

  volatile uint32_t *s = (uint32_t*)0xD0100000;

  s[0] = 0x11111111;
  s[1] = 0x22222222;
  s[2] = 0x33333333;
  s[3] = 0x44444444;

  volatile uint32_t a0 = s[0];
  volatile uint32_t a1 = s[1];
  volatile uint32_t a2 = s[2];
  volatile uint32_t a3 = s[3];
  (void)a0; (void)a1; (void)a2; (void)a3;



  DMA_Status = HAL_MDMA_Start_IT(
  &hmdma_mdma_channel0_sw_0,
  (uint32_t)SRC_Const_Buffer,
  (uint32_t)s,
  BUFFER_SIZE *4,   // word 数
  1
);
  if (DMA_Status !=HAL_OK) {

    while (1);

  }
  uint8_t callbacks = Buffercmp(SRC_Const_Buffer,s,BUFFER_SIZE);


  if (callbacks != 1) {
    while (1);
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_Delay(500);
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
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
  while (1)
  {
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
