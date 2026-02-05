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
#include "fatfs.h"
#include "ltdc.h"
#include "mdma.h"
#include "sdmmc.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "stm32h7xx_hal.h"
#include "lcd.h"
#include "lua_vm.h"
#include "sdram.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define ST_UARTLOG_IMPLEMENTATION

/* LCD 屏幕参数定义 */
#define LCD_WIDTH    800
#define LCD_HEIGHT   480

/* 帧缓冲区配置 */
#define FB_ADDR      0xD0000000u  // SDRAM 起始地址
#define FB_SIZE      (LCD_WIDTH * LCD_HEIGHT * 4)  // ARGB8888 格式，每像素 4 字节
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

#define LCD_W 800
#define LCD_H 480

// 你需要把这两个地址放到你的 SDRAM 映射区（按你工程实际改）
// 常见 H7 FMC SDRAM 映射在 0xC0000000
#define FB0_ADDR 0xD0000000u
#define FB1_ADDR (FB0_ADDR + (LCD_W * LCD_H * 4u))  // 紧挨着放第二个buffer


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ============================================================================
 *                           测试函数实现
 * ============================================================================ */

/**
 * @brief  测试1: 水平线和垂直线
 */
void Test_HorizontalVerticalLines(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 绘制网格 */
    for (uint16_t x = 0; x < LCD_W; x += 50) {
        LCD_DrawVLine(LCD_LAYER0, x, 0, LCD_H, LCD_COLOR_GRAY);
    }

    for (uint16_t y = 0; y < LCD_H; y += 50) {
        LCD_DrawHLine(LCD_LAYER0, 0, y, LCD_W, LCD_COLOR_GRAY);
    }

    /* 绘制彩色水平线 */
    LCD_DrawHLine(LCD_LAYER0, 100, 100, 600, LCD_COLOR_RED);
    LCD_DrawHLine(LCD_LAYER0, 100, 150, 600, LCD_COLOR_GREEN);
    LCD_DrawHLine(LCD_LAYER0, 100, 200, 600, LCD_COLOR_BLUE);

    /* 绘制彩色垂直线 */
    LCD_DrawVLine(LCD_LAYER0, 200, 250, 200, LCD_COLOR_YELLOW);
    LCD_DrawVLine(LCD_LAYER0, 300, 250, 200, LCD_COLOR_CYAN);
    LCD_DrawVLine(LCD_LAYER0, 400, 250, 200, LCD_COLOR_MAGENTA);
}

/**
 * @brief  测试2: 矩形函数
 */
void Test_RectangleFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 实心矩形 */
    LCD_DrawRectFilled(LCD_LAYER0, 50, 50, 150, 100, LCD_COLOR_RED);

    /* 空心矩形(单像素边框) */
    LCD_DrawRectOutline(LCD_LAYER0, 250, 50, 150, 100, 1, LCD_COLOR_GREEN);

    /* 空心矩形(粗边框) */
    LCD_DrawRectOutline(LCD_LAYER0, 450, 50, 150, 100, 5, LCD_COLOR_BLUE);

    /* 嵌套矩形 */
    LCD_DrawRectOutline(LCD_LAYER0, 50, 200, 200, 150, 10, LCD_COLOR_ORANGE);
    LCD_DrawRectOutline(LCD_LAYER0, 70, 220, 160, 110, 5, LCD_COLOR_YELLOW);
    LCD_DrawRectFilled(LCD_LAYER0, 90, 240, 120, 70, LCD_COLOR_CYAN);
}

/**
 * @brief  测试3: 三角形
 */
void Test_TriangleFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 空心三角形 */
    LCD_DrawTriangle(LCD_LAYER0, 150, 50, 50, 200, 250, 200, 2, LCD_COLOR_RED);

    /* 实心三角形 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 450, 50, 350, 200, 550, 200, LCD_COLOR_GREEN);

    /* 各种朝向的三角形 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 150, 250, 50, 400, 250, 400, LCD_COLOR_BLUE);    // 向右
    LCD_DrawTriangleFilled(LCD_LAYER0, 350, 250, 350, 400, 450, 325, LCD_COLOR_YELLOW); // 向左
    LCD_DrawTriangleFilled(LCD_LAYER0, 550, 400, 650, 400, 600, 250, LCD_COLOR_MAGENTA);// 向上
}

/**
 * @brief  测试4: 折线(波形图)
 */
void Test_PolylineFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 正弦波模拟数据 */
    int16_t sine_wave[100];
    for (int i = 0; i < 50; i++) {
        sine_wave[i * 2] = i * 15;                                    // X坐标
        sine_wave[i * 2 + 1] = 240 + (int16_t)(80.0f * sinf(i * 0.3f)); // Y坐标
    }

    /* 绘制正弦波 */
    LCD_DrawPolyline(LCD_LAYER0, sine_wave, 50, 2, LCD_COLOR_RED);

    /* 锯齿波 */
    int16_t sawtooth[20] = {
        50, 100,  100, 150,  150, 100,  200, 150,  250, 100,
        300, 150, 350, 100,  400, 150,  450, 100,  500, 150
    };
    LCD_DrawPolyline(LCD_LAYER0, sawtooth, 10, 3, LCD_COLOR_GREEN);

    /* 折线路径 */
    int16_t path[16] = {
        100, 350,  200, 400,  300, 320,  400, 380,
        500, 300,  600, 360,  700, 280,  750, 350
    };
    LCD_DrawPolyline(LCD_LAYER0, path, 8, 2, LCD_COLOR_CYAN);
}

/**
 * @brief  测试5: 多边形
 */
void Test_PolygonFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 五边形(空心) */
    int16_t pentagon[10] = {
        150, 50,   250, 100,  220, 200,  80, 200,  50, 100
    };
    LCD_DrawPolygon(LCD_LAYER0, pentagon, 5, 2, LCD_COLOR_RED);

    /* 六边形(实心) */
    int16_t hexagon[12] = {
        450, 80,   520, 120,  520, 200,  450, 240,  380, 200,  380, 120
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, hexagon, 6, LCD_COLOR_GREEN);

    /* 星形 */
    int16_t star[10] = {
        200, 280,  220, 340,  280, 350,  230, 390,  250, 450,
    };
    int16_t star2[10] = {
        200, 420,  150, 390,  100, 350,  160, 340,  180, 280
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, star, 5, LCD_COLOR_YELLOW);
    LCD_DrawPolygonFilled(LCD_LAYER0, star2, 5, LCD_COLOR_YELLOW);

    /* 八边形 */
    int16_t octagon[16] = {
        500, 280,  550, 280,  580, 310,  580, 360,
        550, 390,  500, 390,  470, 360,  470, 310
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, octagon, 8, LCD_COLOR_ORANGE);
}

/**
 * @brief  测试6: 椭圆
 */
void Test_EllipseFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 空心椭圆 */
    LCD_DrawEllipse(LCD_LAYER0, 200, 120, 150, 80, 2, LCD_COLOR_RED);
    LCD_DrawEllipse(LCD_LAYER0, 600, 120, 100, 100, 3, LCD_COLOR_BLUE);  // 圆形(特殊椭圆)

    /* 实心椭圆 */
    LCD_DrawEllipseFilled(LCD_LAYER0, 200, 320, 120, 60, LCD_COLOR_GREEN);
    LCD_DrawEllipseFilled(LCD_LAYER0, 600, 320, 80, 120, LCD_COLOR_MAGENTA);

    /* 同心椭圆 */
    LCD_DrawEllipse(LCD_LAYER0, 400, 240, 150, 100, 2, LCD_COLOR_CYAN);
    LCD_DrawEllipse(LCD_LAYER0, 400, 240, 120, 80, 2, LCD_COLOR_YELLOW);
    LCD_DrawEllipse(LCD_LAYER0, 400, 240, 90, 60, 2, LCD_COLOR_ORANGE);
}

/**
 * @brief  测试7: 圆弧
 */
void Test_ArcFunctions(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 四分之一圆弧 */
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 0, 90, 3, LCD_COLOR_RED);      // 0-90度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 90, 180, 3, LCD_COLOR_GREEN);  // 90-180度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 180, 270, 3, LCD_COLOR_BLUE);  // 180-270度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 270, 360, 3, LCD_COLOR_YELLOW);// 270-360度

    /* 半圆弧 */
    LCD_DrawArc(LCD_LAYER0, 500, 120, 80, 0, 180, 5, LCD_COLOR_CYAN);
    LCD_DrawArc(LCD_LAYER0, 500, 360, 80, 180, 360, 5, LCD_COLOR_MAGENTA);

    /* 进度环(270度) */
    LCD_DrawArc(LCD_LAYER0, 650, 240, 100, 135, 405, 8, LCD_COLOR_ORANGE);
}

/**
 * @brief  测试8: 综合图形(绘制房子)
 */
void Test_ComplexGraphics(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 房子主体 */
    LCD_DrawRectFilled(LCD_LAYER0, 250, 200, 300, 250, LCD_COLOR_WHEAT);

    /* 屋顶 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 400, 80, 200, 200, 600, 200, LCD_COLOR_RED);

    /* 门 */
    LCD_DrawRectFilled(LCD_LAYER0, 360, 320, 80, 130, LCD_COLOR_BROWN);
    LCD_DrawCircleFilled(LCD_LAYER0, 420, 390, 5, LCD_COLOR_YELLOW);  // 门把手

    /* 窗户 */
    LCD_DrawRectFilled(LCD_LAYER0, 280, 250, 80, 80, LCD_COLOR_SKYBLUE);
    LCD_DrawRectOutline(LCD_LAYER0, 280, 250, 80, 80, 3, LCD_COLOR_BLACK);
    LCD_DrawHLine(LCD_LAYER0, 280, 290, 80, LCD_COLOR_BLACK);
    LCD_DrawVLine(LCD_LAYER0, 320, 250, 80, LCD_COLOR_BLACK);

    LCD_DrawRectFilled(LCD_LAYER0, 440, 250, 80, 80, LCD_COLOR_SKYBLUE);
    LCD_DrawRectOutline(LCD_LAYER0, 440, 250, 80, 80, 3, LCD_COLOR_BLACK);
    LCD_DrawHLine(LCD_LAYER0, 440, 290, 80, LCD_COLOR_BLACK);
    LCD_DrawVLine(LCD_LAYER0, 480, 250, 80, LCD_COLOR_BLACK);

    /* 烟囱 */
    LCD_DrawRectFilled(LCD_LAYER0, 480, 120, 40, 80, LCD_COLOR_MAROON);

    /* 太阳 */
    LCD_DrawCircleFilled(LCD_LAYER0, 100, 100, 40, LCD_COLOR_YELLOW);

    /* 云朵(用椭圆组合) */
    LCD_DrawEllipseFilled(LCD_LAYER0, 650, 100, 50, 30, LCD_COLOR_WHITE);
    LCD_DrawEllipseFilled(LCD_LAYER0, 690, 110, 40, 25, LCD_COLOR_WHITE);
    LCD_DrawEllipseFilled(LCD_LAYER0, 720, 100, 45, 28, LCD_COLOR_WHITE);

    /* 地面 */
    LCD_DrawRectFilled(LCD_LAYER0, 0, 450, 800, 30, LCD_COLOR_GREEN);
}

/**
 * @brief  测试9: 仪表盘示例
 */
void Test_Dashboard(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 仪表盘外圈 */
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 180, 5, LCD_COLOR_SILVER);
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 160, 2, LCD_COLOR_GRAY);

    /* 刻度圆弧 */
    LCD_DrawArc(LCD_LAYER0, 400, 240, 150, 135, 405, 3, LCD_COLOR_WHITE);

    /* 刻度线(简化版,只画主要刻度) */
    for (int i = 0; i <= 10; i++) {
        int angle = 135 + i * 27;  // 每27度一个刻度
        float rad = angle * 3.14159f / 180.0f;
        int x0 = 400 + (int)(150.0f * cosf(rad));
        int y0 = 240 - (int)(150.0f * sinf(rad));
        int x1 = 400 + (int)(135.0f * cosf(rad));
        int y1 = 240 - (int)(135.0f * sinf(rad));
        LCD_DrawLine(LCD_LAYER0, x0, y0, x1, y1, 2, LCD_COLOR_WHITE);
    }

    /* 指针(指向60%) */
    int pointer_angle = 135 + (int)(270 * 0.6f);  // 60%位置
    float pointer_rad = pointer_angle * 3.14159f / 180.0f;
    int px = 400 + (int)(120.0f * cosf(pointer_rad));
    int py = 240 - (int)(120.0f * sinf(pointer_rad));
    LCD_DrawLine(LCD_LAYER0, 400, 240, px, py, 4, LCD_COLOR_RED);

    /* 中心圆 */
    LCD_DrawCircleFilled(LCD_LAYER0, 400, 240, 15, LCD_COLOR_SILVER);
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 15, 2, LCD_COLOR_BLACK);

    /* 进度环显示60% */
    int arc_end = 135 + (int)(270 * 0.6f);
    LCD_DrawArc(LCD_LAYER0, 400, 240, 140, 135, arc_end, 8, LCD_COLOR_LIME);
}

/**
 * @brief  测试10: 图表示例(柱状图)
 */
void Test_Chart(void)
{
    LCD_Clear(LCD_LAYER0);

    /* 标题区域 */
    LCD_DrawRectFilled(LCD_LAYER0, 0, 0, 800, 50, LCD_COLOR_NAVY);

    /* 绘制坐标轴 */
    LCD_DrawHLine(LCD_LAYER0, 50, 400, 700, LCD_COLOR_WHITE);  // X轴
    LCD_DrawVLine(LCD_LAYER0, 50, 80, 320, LCD_COLOR_WHITE);   // Y轴

    /* Y轴刻度 */
    for (int i = 0; i <= 5; i++) {
        int y = 400 - i * 64;
        LCD_DrawHLine(LCD_LAYER0, 45, y, 10, LCD_COLOR_WHITE);
    }

    /* 柱状图数据 */
    uint16_t data[6] = {80, 150, 120, 200, 90, 180};  // 6个数据点
    uint32_t colors[6] = {
        LCD_COLOR_RED, LCD_COLOR_BLUE, LCD_COLOR_GREEN,
        LCD_COLOR_YELLOW, LCD_COLOR_CYAN, LCD_COLOR_MAGENTA
    };

    /* 绘制柱状图 */
    for (int i = 0; i < 6; i++) {
        uint16_t x = 100 + i * 110;
        uint16_t height = data[i];
        uint16_t y = 400 - height;

        /* 柱子 */
        LCD_DrawRectFilled(LCD_LAYER0, x, y, 80, height, colors[i]);
        LCD_DrawRectOutline(LCD_LAYER0, x, y, 80, height, 2, LCD_COLOR_BLACK);
    }

    /* 网格线 */
    for (int i = 1; i < 5; i++) {
        int y = 400 - i * 64;
        LCD_DrawHLine(LCD_LAYER0, 50, y, 700, LCD_COLOR_GRAY);
    }
}


static FATFS fs;
static FIL fil;

static void list_dir(const char *path)
{
    FRESULT fr;
    DIR dir;
    FILINFO fno;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("opendir %s failed: %d\r\n", path, fr);
        return;
    }

    printf("=== list: %s ===\r\n", path);
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        // 只打印文件名（不开 LFN 也能用）
        printf("%c  %s\r\n", (fno.fattrib & AM_DIR) ? 'D' : 'F', fno.fname);
    }

    f_closedir(&dir);
}

void fatfs_min_test(void)
{
    FRESULT fr;

    // 1) 挂载（CubeMX 默认盘符通常是 "0:"）
    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
        printf("mount failed: %d\r\n", fr);
        return;
    }

    // 2) 列根目录
    list_dir("0:/");

    // 3) 写文件
    fr = f_open(&fil, "0:/hello.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("open write failed: %d\r\n", fr);
        return;
    }

    const char *msg = "hello sdmmc + fatfs\r\n";
    UINT bw = 0;
    fr = f_write(&fil, msg, (UINT)strlen(msg), &bw);
    f_close(&fil);

    if (fr != FR_OK) {
        printf("write failed: %d\r\n", fr);
        return;
    }
    printf("write ok, bytes=%u\r\n", bw);

    // 4) 读文件
    fr = f_open(&fil, "0:/hello.txt", FA_READ);
    if (fr != FR_OK) {
        printf("open read failed: %d\r\n", fr);
        return;
    }

    char buf[128] = {0};
    UINT br = 0;
    fr = f_read(&fil, buf, sizeof(buf) - 1, &br);
    f_close(&fil);

    if (fr != FR_OK) {
        printf("read failed: %d\r\n", fr);
        return;
    }

    printf("read ok, bytes=%u, content:\r\n%s\r\n", br, buf);

    // 5) 再列一次目录（应该能看到 hello.txt）
    list_dir("0:/");

    // 可选：卸载
    // f_mount(NULL, "0:", 0);
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
  /* USER CODE BEGIN 2 */

  /* 初始化 SDRAM */
  SDRAM_Init();

  /* 使能 LCD 显示 */

    // fatfs_min_test();
    lua_demo_blink();


    // LCD_Clear(0);
    // LCD_Clear(1);
    // LCD_Refresh(0);
    // LCD_Refresh(1);
    //
    // LCD_DrawRectFilled(LCD_LAYER0, 70, 50, 150, 100, 0x000000FF);
    // LCD_DrawRectFilled(LCD_LAYER0, 50, 50, 150, 100, 0x99FF00FF);

    // // 底层：黑底 + 白色实心方块
    // LCD_Fill(LCD_LAYER0, LCD_COLOR_BLACK);
    // LCD_DrawRect(LCD_LAYER0, 200, 120, 240, 240, LCD_COLOR_WHITE);
    //
    // // 顶层：透明底 + “偏粉的红”方块（与白块有重叠）
    // LCD_Fill(LCD_LAYER1, 0x00000000u);               // 全透明背景
    // LCD_DrawRect(LCD_LAYER1, 260, 160, 240, 240, 0xCCFF8197u); // 关键颜色
    //
    // // 顶层半透明：让叠加后的颜色落在粉色附近
    // LCD_SetLayerVisible(LCD_LAYER0, 1);
    // LCD_SetLayerVisible(LCD_LAYER1, 1);
    // LCD_SetTransparency(LCD_LAYER1, 128);
    //
    // LCD_Refresh(LCD_LAYER0);
    // LCD_Refresh(LCD_LAYER1);
    // LCD_DisplayON();
    //
    // HAL_Delay(2000);
    //
    // LCD_Clear(0);
    // LCD_Clear(1);
    // LCD_Refresh(0);
    // LCD_Refresh(1);
    //
    // Test_HorizontalVerticalLines();
    // HAL_Delay(2000);
    // Test_RectangleFunctions();
    // HAL_Delay(2000);
    // Test_TriangleFunctions();
    // HAL_Delay(2000);
    // Test_PolylineFunctions();
    // HAL_Delay(2000);
    // Test_PolygonFunctions();
    // HAL_Delay(2000);
    // Test_EllipseFunctions();
    // HAL_Delay(2000);
    // Test_ArcFunctions();
    // HAL_Delay(2000);
    // Test_ComplexGraphics();
    // HAL_Delay(2000);
    // Test_Dashboard();
    // HAL_Delay(2000);
    // Test_Chart();
    // HAL_Delay(2000);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* LED 闪烁，指示系统运行 */
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
