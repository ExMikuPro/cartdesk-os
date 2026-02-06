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
#include "../Driver/LCD/lcd.h"
#include "lua_vm.h"
#include "../Driver/SDRAM/sdram.h"
#include "Core/Screen/Page/ui_screen_launcher.h"
#include "CRC/crc.h"
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
void Test_HorizontalVerticalLines(void) {
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
void Test_RectangleFunctions(void) {
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
void Test_TriangleFunctions(void) {
    LCD_Clear(LCD_LAYER0);

    /* 空心三角形 */
    LCD_DrawTriangle(LCD_LAYER0, 150, 50, 50, 200, 250, 200, 2, LCD_COLOR_RED);

    /* 实心三角形 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 450, 50, 350, 200, 550, 200, LCD_COLOR_GREEN);

    /* 各种朝向的三角形 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 150, 250, 50, 400, 250, 400, LCD_COLOR_BLUE); // 向右
    LCD_DrawTriangleFilled(LCD_LAYER0, 350, 250, 350, 400, 450, 325, LCD_COLOR_YELLOW); // 向左
    LCD_DrawTriangleFilled(LCD_LAYER0, 550, 400, 650, 400, 600, 250, LCD_COLOR_MAGENTA); // 向上
}

/**
 * @brief  测试4: 折线(波形图)
 */
void Test_PolylineFunctions(void) {
    LCD_Clear(LCD_LAYER0);

    /* 正弦波模拟数据 */
    int16_t sine_wave[100];
    for (int i = 0; i < 50; i++) {
        sine_wave[i * 2] = i * 15; // X坐标
        sine_wave[i * 2 + 1] = 240 + (int16_t) (80.0f * sinf(i * 0.3f)); // Y坐标
    }

    /* 绘制正弦波 */
    LCD_DrawPolyline(LCD_LAYER0, sine_wave, 50, 2, LCD_COLOR_RED);

    /* 锯齿波 */
    int16_t sawtooth[20] = {
        50, 100, 100, 150, 150, 100, 200, 150, 250, 100,
        300, 150, 350, 100, 400, 150, 450, 100, 500, 150
    };
    LCD_DrawPolyline(LCD_LAYER0, sawtooth, 10, 3, LCD_COLOR_GREEN);

    /* 折线路径 */
    int16_t path[16] = {
        100, 350, 200, 400, 300, 320, 400, 380,
        500, 300, 600, 360, 700, 280, 750, 350
    };
    LCD_DrawPolyline(LCD_LAYER0, path, 8, 2, LCD_COLOR_CYAN);
}

/**
 * @brief  测试5: 多边形
 */
void Test_PolygonFunctions(void) {
    LCD_Clear(LCD_LAYER0);

    /* 五边形(空心) */
    int16_t pentagon[10] = {
        150, 50, 250, 100, 220, 200, 80, 200, 50, 100
    };
    LCD_DrawPolygon(LCD_LAYER0, pentagon, 5, 2, LCD_COLOR_RED);

    /* 六边形(实心) */
    int16_t hexagon[12] = {
        450, 80, 520, 120, 520, 200, 450, 240, 380, 200, 380, 120
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, hexagon, 6, LCD_COLOR_GREEN);

    /* 星形 */
    int16_t star[10] = {
        200, 280, 220, 340, 280, 350, 230, 390, 250, 450,
    };
    int16_t star2[10] = {
        200, 420, 150, 390, 100, 350, 160, 340, 180, 280
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, star, 5, LCD_COLOR_YELLOW);
    LCD_DrawPolygonFilled(LCD_LAYER0, star2, 5, LCD_COLOR_YELLOW);

    /* 八边形 */
    int16_t octagon[16] = {
        500, 280, 550, 280, 580, 310, 580, 360,
        550, 390, 500, 390, 470, 360, 470, 310
    };
    LCD_DrawPolygonFilled(LCD_LAYER0, octagon, 8, LCD_COLOR_ORANGE);
}

/**
 * @brief  测试6: 椭圆
 */
void Test_EllipseFunctions(void) {
    LCD_Clear(LCD_LAYER0);

    /* 空心椭圆 */
    LCD_DrawEllipse(LCD_LAYER0, 200, 120, 150, 80, 2, LCD_COLOR_RED);
    LCD_DrawEllipse(LCD_LAYER0, 600, 120, 100, 100, 3, LCD_COLOR_BLUE); // 圆形(特殊椭圆)

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
void Test_ArcFunctions(void) {
    LCD_Clear(LCD_LAYER0);

    /* 四分之一圆弧 */
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 0, 90, 3, LCD_COLOR_RED); // 0-90度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 90, 180, 3, LCD_COLOR_GREEN); // 90-180度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 180, 270, 3, LCD_COLOR_BLUE); // 180-270度
    LCD_DrawArc(LCD_LAYER0, 200, 240, 100, 270, 360, 3, LCD_COLOR_YELLOW); // 270-360度

    /* 半圆弧 */
    LCD_DrawArc(LCD_LAYER0, 500, 120, 80, 0, 180, 5, LCD_COLOR_CYAN);
    LCD_DrawArc(LCD_LAYER0, 500, 360, 80, 180, 360, 5, LCD_COLOR_MAGENTA);

    /* 进度环(270度) */
    LCD_DrawArc(LCD_LAYER0, 650, 240, 100, 135, 405, 8, LCD_COLOR_ORANGE);
}

/**
 * @brief  测试8: 综合图形(绘制房子)
 */
void Test_ComplexGraphics(void) {
    LCD_Clear(LCD_LAYER0);

    /* 房子主体 */
    LCD_DrawRectFilled(LCD_LAYER0, 250, 200, 300, 250, LCD_COLOR_WHEAT);

    /* 屋顶 */
    LCD_DrawTriangleFilled(LCD_LAYER0, 400, 80, 200, 200, 600, 200, LCD_COLOR_RED);

    /* 门 */
    LCD_DrawRectFilled(LCD_LAYER0, 360, 320, 80, 130, LCD_COLOR_BROWN);
    LCD_DrawCircleFilled(LCD_LAYER0, 420, 390, 5, LCD_COLOR_YELLOW); // 门把手

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
void Test_Dashboard(void) {
    LCD_Clear(LCD_LAYER0);

    /* 仪表盘外圈 */
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 180, 5, LCD_COLOR_SILVER);
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 160, 2, LCD_COLOR_GRAY);

    /* 刻度圆弧 */
    LCD_DrawArc(LCD_LAYER0, 400, 240, 150, 135, 405, 3, LCD_COLOR_WHITE);

    /* 刻度线(简化版,只画主要刻度) */
    for (int i = 0; i <= 10; i++) {
        int angle = 135 + i * 27; // 每27度一个刻度
        float rad = angle * 3.14159f / 180.0f;
        int x0 = 400 + (int) (150.0f * cosf(rad));
        int y0 = 240 - (int) (150.0f * sinf(rad));
        int x1 = 400 + (int) (135.0f * cosf(rad));
        int y1 = 240 - (int) (135.0f * sinf(rad));
        LCD_DrawLine(LCD_LAYER0, x0, y0, x1, y1, 2, LCD_COLOR_WHITE);
    }

    /* 指针(指向60%) */
    int pointer_angle = 135 + (int) (270 * 0.6f); // 60%位置
    float pointer_rad = pointer_angle * 3.14159f / 180.0f;
    int px = 400 + (int) (120.0f * cosf(pointer_rad));
    int py = 240 - (int) (120.0f * sinf(pointer_rad));
    LCD_DrawLine(LCD_LAYER0, 400, 240, px, py, 4, LCD_COLOR_RED);

    /* 中心圆 */
    LCD_DrawCircleFilled(LCD_LAYER0, 400, 240, 15, LCD_COLOR_SILVER);
    LCD_DrawCircle(LCD_LAYER0, 400, 240, 15, 2, LCD_COLOR_BLACK);

    /* 进度环显示60% */
    int arc_end = 135 + (int) (270 * 0.6f);
    LCD_DrawArc(LCD_LAYER0, 400, 240, 140, 135, arc_end, 8, LCD_COLOR_LIME);
}

/**
 * @brief  测试10: 图表示例(柱状图)
 */
void Test_Chart(void) {
    LCD_Clear(LCD_LAYER0);

    /* 标题区域 */
    LCD_DrawRectFilled(LCD_LAYER0, 0, 0, 800, 50, LCD_COLOR_NAVY);

    /* 绘制坐标轴 */
    LCD_DrawHLine(LCD_LAYER0, 50, 400, 700, LCD_COLOR_WHITE); // X轴
    LCD_DrawVLine(LCD_LAYER0, 50, 80, 320, LCD_COLOR_WHITE); // Y轴

    /* Y轴刻度 */
    for (int i = 0; i <= 5; i++) {
        int y = 400 - i * 64;
        LCD_DrawHLine(LCD_LAYER0, 45, y, 10, LCD_COLOR_WHITE);
    }

    /* 柱状图数据 */
    uint16_t data[6] = {80, 150, 120, 200, 90, 180}; // 6个数据点
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

static void list_dir(const char *path) {
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

void fatfs_min_test(void) {
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
    fr = f_write(&fil, msg, (UINT) strlen(msg), &bw);
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

void demo_crc(void) {
    CRC_ASSERT_INT32();

    uint8_t u8[] = {1, 2, 3, 4, 0xAA};
    uint16_t u16[] = {0x1122, 0x3344, 0x5566};
    uint32_t u32[] = {0x11223344, 0xAABBCCDD};

    int8_t i8[] = {-1, 2, -3, 4};
    int16_t i16[] = {-1000, 2000, -3000};
    int32_t i32[] = {-12345678, 23456789};
    int ii[] = {1, -2, 3, -4};

    /* 1) one-shot：数组 → 自动格式+自动长度 */
    uint32_t c1 = CRC_Calc(&hcrc, u8); // bytes
    uint32_t c2 = CRC_Calc(&hcrc, u16); // u16
    uint32_t c3 = CRC_Calc(&hcrc, u32); // u32
    uint32_t c4 = CRC_Calc(&hcrc, i8); // bytes
    uint32_t c5 = CRC_Calc(&hcrc, i16); // u16
    uint32_t c6 = CRC_Calc(&hcrc, i32); // u32
    uint32_t c7 = CRC_Calc(&hcrc, ii); // u32(int)

    /* 2) one-shot：指针+长度 → 自动格式 + 你指定长度 */
    uint32_t c8 = CRC_Calc(&hcrc, (uint8_t*)u8, 3); // 只算前3个字节
    uint32_t c9 = CRC_Calc(&hcrc, (uint16_t*)u16, 2); // 只算前2个halfword

    /* 3) 流式：同名 CRC_Update */
    CRC_Begin(&hcrc);
    CRC_Update(&hcrc, u8); // 数组：自动长度
    CRC_Update(&hcrc, (uint32_t*)u32, 1); // 指针+长度：只喂1个word
    uint32_t c10 = CRC_Final(&hcrc);

    (void) c1;
    (void) c2;
    (void) c3;
    (void) c4;
    (void) c5;
    (void) c6;
    (void) c7;
    (void) c8;
    (void) c9;
    (void) c10;
}

static void on_launch(int index) {
    // TODO: 这里切换到你的“程序”/“脚本”
    // 例如：load_lua_app(index);
}



// ========== 可选：如果你已经做了 LTDC LineEvent/VBlank 旗标，可以开这个对比“有/无 VSync” ==========
#ifndef TEARTEST_USE_VBLANK
#define TEARTEST_USE_VBLANK 0
#endif

#if TEARTEST_USE_VBLANK
extern volatile uint8_t  g_ltdc_vblank_flag;   // 你自己的 LineEvent 回调里置 1
static inline void TearTest_WaitVBlank(void) {
    while (!g_ltdc_vblank_flag) {}
    g_ltdc_vblank_flag = 0;
}
#else
static inline void TearTest_WaitVBlank(void) { (void)0; }
#endif

static inline uint32_t ARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a<<24) | ((uint32_t)r<<16) | ((uint32_t)g<<8) | (uint32_t)b;
}

// 调一次：清背景，准备测试
void LCD_TearTest_Init(void)
{
    LCD_Clear(0);
    LCD_Clear(1);

    // 背景固定（layer0）
    LCD_Fill(0, ARGB(0xFF, 0x10, 0x12, 0x18));
    LCD_Refresh(0);

    // 覆盖层（layer1）清成透明（如果你的 layer1 支持 alpha 混合）
    LCD_Fill(1, ARGB(0x00, 0x00, 0x00, 0x00));
    LCD_Refresh(1);
}

/**
 * 在 while(1) 里一直调用：动态绘制“整屏移动条纹 + 贯穿全屏的竖条”
 * 如果是单缓冲且不做 VBlank 翻页，最容易看到水平撕裂断层。
 */
void LCD_TearTest_Loop(void)
{
    static uint32_t last = 0;
    static int phase = 0;

    uint32_t now = HAL_GetTick();
    if (last == 0) last = now;
    uint32_t dt = now - last;
    last = now;

    // 速度：每 16ms 前进 8 像素（你可以调大更容易撕裂）
    (void)dt;
    phase = (phase + 8) % 64;

    // 对比测试：若你开了 TEARTEST_USE_VBLANK，这里会等到 VBlank 再动手画
    TearTest_WaitVBlank();

    // 每帧重画 layer1（覆盖层）
    LCD_Fill(1, ARGB(0x00, 0x00, 0x00, 0x00));   // 透明清屏（overlay）
    // 如果你 layer1 不支持透明，就把 alpha 改 FF 并选深色

    // 1) 整屏横向条纹（移动）：特别容易看出“同一帧上下相位不一致”的撕裂
    const int stripe_h = 16;
    for (int y = 0; y < (int)LCD_H; y += stripe_h) {
        int on = ((y + phase) / stripe_h) & 1;
        uint32_t c = on ? ARGB(0xFF, 0xF2, 0xF6, 0xFF) : ARGB(0xFF, 0x2A, 0x33, 0x42);
        LCD_DrawRectFilled(1, 0, (uint16_t)y, LCD_W, (uint16_t)stripe_h, c);
    }

    // 2) 贯穿全屏的竖条（移动）：撕裂时竖条会在某个水平位置“断开错位”
    int x = (phase * 12) % (int)(LCD_W + 80) - 40;     // 左右穿出屏幕
    int w = 20;
    if (x < 0) { w += x; x = 0; }
    if (x + w > (int)LCD_W) w = (int)LCD_W - x;
    if (w > 0) {
        LCD_DrawRectFilled(1, (uint16_t)x, 0, (uint16_t)w, LCD_H, ARGB(0xFF, 0x3D, 0xCC, 0xA3));
        LCD_DrawRectOutline(1, (uint16_t)x, 0, (uint16_t)w, LCD_H, 2, ARGB(0xFF, 0x00, 0x00, 0x00));
    }

    LCD_Refresh(1);
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
  MX_FMC_Init();
  MX_LTDC_Init();
  MX_USART1_UART_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  MX_CRC_Init();
  MX_DMA2D_Init();
  /* USER CODE BEGIN 2 */

    /* 初始化 SDRAM */
    SDRAM_Init();

    // demo_crc();

    /* 使能 LCD 显示 */
    HAL_GPIO_WritePin(BUZZER_GPIO_Port,BUZZER_Pin, GPIO_PIN_RESET); // 蜂鸣器高响低不响


    LCD_DoubleBufferInit();
    // LCD_TearTest_Init();

    // ui_screen_launcher_init();
    // ui_screen_launcher_set_on_launch(on_launch);
    // ui_screen_launcher_enter();


    // fatfs_min_test();
    // lua_demo_blink();


    // int i = 0;
    // Launcher_Init();

    static uint8_t pa0_prev = 0, pc13_prev = 0;


    //
    // LCD_DrawRectFilled(LCD_LAYER0, 70, 50, 150, 100, 0x000000FF);
    // LCD_DrawRectFilled(LCD_LAYER0, 50, 50, 150, 100, 0x99FF00FF);
    //
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
    // int x = 0;
    Launcher_Init();

    LCD_DisplayON();
    // for (int a = 0; a < 5; a++) {
    //     LCD_DrawRectOutline(1,10+(a*150),10,100,100,2,ARGB(0xFF, 0xAA, 0xAA, 0xAA));
    // }

    // HAL_Delay(2000);
    // while (1) {}
    int selected_app = 0;



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1) {
        // LCD_Clear(1);
        // LCD_DrawRectOutline(1, x, 200, 100, 100, 3, 0xFFFF0000);
        // LCD_Refresh(1);
        //
        // x += 10;
        // if (x > 700) x = 0;
        // HAL_Delay(16); // ~60fps
        // LCD_TearTest_Loop();
        // HAL_Delay(50);
        /* LED 闪烁，指示系统运行 */
        uint8_t pa0  = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)  == GPIO_PIN_SET);
        uint8_t pc13 = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET);

        if (pa0 && !pa0_prev)  selected_app++;
        if (pc13 && !pc13_prev) selected_app--;

        pa0_prev  = pa0;
        pc13_prev = pc13;
        // 自动节流到60Hz
        Launcher_Loop(&selected_app);
        // HAL_Delay(16);
        // HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
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
