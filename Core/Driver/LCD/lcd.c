/**
 * @file    lcd.c
 * @brief   LCD驱动实现文件
 * @details 基于STM32 LTDC控制器的图形库实现
 *          支持ARGB8888格式、双层显示、DCache一致性管理
 */

#include "lcd.h"
#include "ltdc.h"

/* ============================================================================
 *                           私有变量
 * ============================================================================ */

/** @brief 当前活动图层索引 */
static uint32_t ActiveLayer = 0;


/* ============================================================================
 *                           私有函数声明
 * ============================================================================ */

/**
 * @brief  清理DCache指定地址范围(确保数据写入SDRAM)
 * @param  addr: 起始地址
 * @param  size: 数据大小(字节)
 * @note   自动对齐到32字节Cache Line边界
 */
static inline void LCD_DCacheClean(void *addr, uint32_t size);

/**
 * @brief  快速绘制矩形(不立即刷新DCache)
 * @param  Layer: 图层索引
 * @param  X: 起始X坐标
 * @param  Y: 起始Y坐标
 * @param  W: 宽度
 * @param  H: 高度
 * @param  Color: 颜色
 * @note   仅修改缓冲区,需配合DCache清理使用
 */
static void LCD_DrawRectFast(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color);

/**
 * @brief  快速绘制像素(不立即刷新DCache)
 * @param  Layer: 图层索引
 * @param  x: X坐标
 * @param  y: Y坐标
 * @param  Color: 颜色
 */
static inline void LCD_DrawPixelFast(uint8_t Layer, int x, int y, uint32_t Color);


/* ============================================================================
 *                           图层控制函数实现
 * ============================================================================ */

/**
 * @brief  选择当前操作的图层
 */
void LCD_Select(uint32_t LayerIndex)
{
    ActiveLayer = LayerIndex;
}

/**
 * @brief  获取当前图层的宽度
 */
uint32_t LCD_GetXSize(void)
{
    return hltdc.LayerCfg[ActiveLayer].ImageWidth;
}

/**
 * @brief  获取当前图层的高度
 */
uint32_t LCD_GetYSize(void)
{
    return hltdc.LayerCfg[ActiveLayer].ImageHeight;
}

/**
 * @brief  打开LCD显示
 */
void LCD_DisplayON(void)
{
    __HAL_LTDC_ENABLE(&hltdc);                                      // 使能LTDC控制器
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);  // 打开背光
}

/**
 * @brief  关闭LCD显示
 */
void LCD_DisplayOFF(void)
{
    __HAL_LTDC_DISABLE(&hltdc);                                       // 禁用LTDC控制器
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);  // 关闭背光
}

/**
 * @brief  设置图层透明度
 */
void LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency)
{
    HAL_LTDC_SetAlpha(&hltdc, LayerIndex, Transparency);
}

/**
 * @brief  设置图层可见性
 */
void LCD_SetLayerVisible(uint32_t LayerIndex, uint8_t Status)
{
    if (Status) {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, LayerIndex);   // 显示图层
    } else {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, LayerIndex);  // 隐藏图层
    }
}


/* ============================================================================
 *                           DCache管理函数
 * ============================================================================ */

/**
 * @brief  清理DCache指定范围
 * @note   将CPU缓存中的数据刷新到SDRAM,确保LTDC能读取到最新数据
 */
static inline void LCD_DCacheClean(void *addr, uint32_t size)
{
    uint32_t start = (uint32_t)addr;
    uint32_t aligned_start = start & ~31u;          // 向下对齐到32字节边界
    uint32_t end = start + size;
    uint32_t aligned_end = (end + 31u) & ~31u;      // 向上对齐到32字节边界
    
    SCB_CleanDCache_by_Addr((uint32_t*)aligned_start, (int32_t)(aligned_end - aligned_start));
}


/* ============================================================================
 *                           帧缓冲区访问函数
 * ============================================================================ */

/**
 * @brief  获取指定图层的帧缓冲区地址
 */
uint32_t* LCD_GetFB(uint8_t Layer)
{
    if (Layer == LCD_LAYER0) {
        return (uint32_t*)LCD_FB0_ADDR;
    } else {
        return (uint32_t*)LCD_FB1_ADDR;
    }
}


/* ============================================================================
 *                           基础绘图函数实现
 * ============================================================================ */

/**
 * @brief  清空指定图层(填充黑色)
 */
void LCD_Clear(uint8_t Layer)
{
    LCD_Fill(Layer, 0x00000000);
}

/**
 * @brief  用指定颜色填充整个图层
 */
void LCD_Fill(uint8_t Layer, uint32_t Color)
{
    /* 填充所有像素 */
    uint32_t *fb = LCD_GetFB(Layer);
    for (uint32_t i = 0; i < LCD_W * LCD_H; i++) {
        fb[i] = Color;
    }

    /* 清理DCache,确保数据写入外部SDRAM */
    SCB_CleanDCache_by_Addr((uint32_t*)fb, FB_SIZE);
}

/**
 * @brief  快速绘制矩形(内部函数,不刷新DCache)
 */
static void LCD_DrawRectFast(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color)
{
    /* 边界检查 */
    if (W == 0 || H == 0) return;
    if (X >= LCD_W || Y >= LCD_H) return;

    /* 裁剪到屏幕范围 */
    uint32_t x1 = (uint32_t)X + W;
    if (x1 > LCD_W) x1 = LCD_W;
    uint32_t y1 = (uint32_t)Y + H;
    if (y1 > LCD_H) y1 = LCD_H;

    /* 填充矩形区域 */
    uint32_t *fb = LCD_GetFB(Layer);
    for (uint32_t yy = (uint32_t)Y; yy < y1; yy++) {
        uint32_t *row = fb + yy * (uint32_t)LCD_W + (uint32_t)X;
        for (uint32_t xx = (uint32_t)X; xx < x1; xx++) {
            row[xx - (uint32_t)X] = Color;
        }
    }
}

/**
 * @brief  绘制实心矩形(立即可见)
 */
void LCD_DrawRect(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color)
{
    /* 先绘制矩形(不刷新缓存) */
    LCD_DrawRectFast(Layer, X, Y, W, H, Color);

    /* 边界检查 */
    if (W == 0 || H == 0) return;
    if (X >= LCD_W || Y >= LCD_H) return;

    /* 计算实际绘制区域 */
    uint32_t x1 = (uint32_t)X + W;
    if (x1 > LCD_W) x1 = LCD_W;
    uint32_t y1 = (uint32_t)Y + H;
    if (y1 > LCD_H) y1 = LCD_H;
    
    uint32_t rect_w = x1 - (uint32_t)X;
    uint32_t rect_h = y1 - (uint32_t)Y;

    /* 计算需要清理的DCache范围 */
    uint32_t *fb = LCD_GetFB(Layer);
    uint32_t *start = fb + (uint32_t)Y * (uint32_t)LCD_W + (uint32_t)X;

    /* 连续覆盖区:从第一行起到最后一行末尾 */
    uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
    LCD_DCacheClean(start, bytes);
}

/**
 * @brief  绘制方形点(笔刷)
 */
void LCD_DrawPoint(uint8_t Layer, uint16_t X, uint16_t Y, uint8_t Size, uint32_t Color)
{
    if (Size == 0) Size = 1;
    LCD_DrawRect(Layer, X, Y, (uint16_t)Size, (uint16_t)Size, Color);
}

/**
 * @brief  快速绘制像素(内部函数,不刷新DCache)
 */
static inline void LCD_DrawPixelFast(uint8_t Layer, int x, int y, uint32_t Color)
{
    /* 边界检查 */
    if ((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) return;
    
    /* 写入像素 */
    uint32_t *fb = LCD_GetFB(Layer);
    fb[(uint32_t)y * (uint32_t)LCD_W + (uint32_t)x] = Color;
}

/**
 * @brief  绘制单个像素(立即可见)
 */
void LCD_DrawPixel(uint8_t Layer, uint16_t X, uint16_t Y, uint32_t Color)
{
    /* 边界检查 */
    if (X >= LCD_W || Y >= LCD_H) return;

    /* 先用Fast写像素 */
    LCD_DrawPixelFast(Layer, (int)X, (int)Y, Color);

    /* 再清理对应的4字节(内部会对齐到32B cache line) */
    uint32_t *fb = LCD_GetFB(Layer);
    uint32_t *p = fb + (uint32_t)Y * (uint32_t)LCD_W + (uint32_t)X;
    LCD_DCacheClean(p, 4u);
}

/**
 * @brief  绘制直线(Bresenham算法)
 */
void LCD_DrawLine(uint8_t Layer, int x0, int y0, int x1, int y1, uint8_t Size, uint32_t Color)
{
    /* 记录脏矩形边界(用于最后一次清理DCache) */
    int minx = x0, maxx = x0, miny = y0, maxy = y0;

    /* Bresenham直线算法初始化 */
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    /* 逐点绘制直线 */
    while (1) {
        /* 根据笔刷尺寸绘制点 */
        if (Size <= 1) {
            LCD_DrawPixelFast(Layer, x0, y0, Color);  // 单像素
        } else {
            /* 绘制Size×Size方块作为笔刷点 */
            int r = (int)Size - 1;
            LCD_DrawRectFast(Layer,
                           (uint16_t)(x0 - r), (uint16_t)(y0 - r),
                           Size, Size, Color);
        }

        /* 更新脏矩形边界 */
        if (x0 < minx) minx = x0;
        if (x0 > maxx) maxx = x0;
        if (y0 < miny) miny = y0;
        if (y0 > maxy) maxy = y0;

        /* 判断是否到达终点 */
        if (x0 == x1 && y0 == y1) break;

        /* Bresenham误差修正 */
        int e2 = err << 1;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }

    /* 线条绘制完成,清理DCache(立即更新显示) */
    /* 注意:Size>1时,脏矩形要扩大Size像素 */
    int pad = (Size <= 1) ? 0 : (int)Size;
    minx -= pad; miny -= pad;
    maxx += pad; maxy += pad;

    /* 裁剪到屏幕范围 */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    uint32_t rect_w = (uint32_t)(maxx - minx + 1);
    uint32_t rect_h = (uint32_t)(maxy - miny + 1);

    /* 计算DCache清理范围 */
    uint32_t *fb = LCD_GetFB(Layer);
    uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
    uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;

    LCD_DCacheClean(start, bytes);
}


/* ============================================================================
 *                           圆形绘制函数实现
 * ============================================================================ */

/**
 * @brief  绘制实心圆
 * @note   使用中点圆算法 + 扫描线填充优化
 */
void LCD_DrawCircleFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint32_t Color)
{
    if (Radius == 0) return;

    /* 记录脏矩形边界 */
    int minx = (int)CenterX - (int)Radius;
    int maxx = (int)CenterX + (int)Radius;
    int miny = (int)CenterY - (int)Radius;
    int maxy = (int)CenterY + (int)Radius;

    /* 中点圆算法:只计算1/8圆弧,利用对称性绘制8个象限 */
    int x = 0;
    int y = (int)Radius;
    int d = 3 - 2 * (int)Radius;  // 判断误差

    /* 绘制圆心水平线 */
    int cx = (int)CenterX;
    int cy = (int)CenterY;
    LCD_DrawRectFast(Layer, (uint16_t)(cx - y), (uint16_t)cy, (uint16_t)(2 * y + 1), 1, Color);

    while (x <= y) {
        /* 绘制4条水平扫描线(利用对称性) */
        if (x != 0) {
            /* 上下两条x轴对称的水平线 */
            LCD_DrawRectFast(Layer, (uint16_t)(cx - y), (uint16_t)(cy + x), (uint16_t)(2 * y + 1), 1, Color);
            LCD_DrawRectFast(Layer, (uint16_t)(cx - y), (uint16_t)(cy - x), (uint16_t)(2 * y + 1), 1, Color);
        }
        
        if (x != y) {
            /* 左右两条y轴对称的水平线 */
            LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy + y), (uint16_t)(2 * x + 1), 1, Color);
            LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy - y), (uint16_t)(2 * x + 1), 1, Color);
        }

        /* 更新中点圆算法参数 */
        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }

    /* 裁剪脏矩形到屏幕范围 */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    /* 清理DCache */
    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}

/**
 * @brief  绘制空心圆
 * @note   使用中点圆算法,支持粗线宽
 */
void LCD_DrawCircle(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint8_t LineWidth, uint32_t Color)
{
    if (Radius == 0 || LineWidth == 0) return;

    /* 记录脏矩形边界 */
    int pad = (int)LineWidth;
    int minx = (int)CenterX - (int)Radius - pad;
    int maxx = (int)CenterX + (int)Radius + pad;
    int miny = (int)CenterY - (int)Radius - pad;
    int maxy = (int)CenterY + (int)Radius + pad;

    /* 中点圆算法:计算圆周上的点 */
    int x = 0;
    int y = (int)Radius;
    int d = 3 - 2 * (int)Radius;
    int cx = (int)CenterX;
    int cy = (int)CenterY;

    /* 绘制辅助宏:在8个对称点绘制方块笔刷 */
    #define DRAW_CIRCLE_POINT(px, py) \
        do { \
            if (LineWidth == 1) { \
                LCD_DrawPixelFast(Layer, px, py, Color); \
            } else { \
                int half = (int)LineWidth / 2; \
                LCD_DrawRectFast(Layer, (uint16_t)((px) - half), (uint16_t)((py) - half), \
                               LineWidth, LineWidth, Color); \
            } \
        } while(0)

    /* 绘制初始8个对称点 */
    DRAW_CIRCLE_POINT(cx + x, cy + y);
    DRAW_CIRCLE_POINT(cx - x, cy + y);
    DRAW_CIRCLE_POINT(cx + x, cy - y);
    DRAW_CIRCLE_POINT(cx - x, cy - y);
    DRAW_CIRCLE_POINT(cx + y, cy + x);
    DRAW_CIRCLE_POINT(cx - y, cy + x);
    DRAW_CIRCLE_POINT(cx + y, cy - x);
    DRAW_CIRCLE_POINT(cx - y, cy - x);

    while (x < y) {
        /* 更新中点圆算法参数 */
        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;

        /* 绘制当前位置的8个对称点 */
        DRAW_CIRCLE_POINT(cx + x, cy + y);
        DRAW_CIRCLE_POINT(cx - x, cy + y);
        DRAW_CIRCLE_POINT(cx + x, cy - y);
        DRAW_CIRCLE_POINT(cx - x, cy - y);
        DRAW_CIRCLE_POINT(cx + y, cy + x);
        DRAW_CIRCLE_POINT(cx - y, cy + x);
        DRAW_CIRCLE_POINT(cx + y, cy - x);
        DRAW_CIRCLE_POINT(cx - y, cy - x);
    }

    #undef DRAW_CIRCLE_POINT

    /* 裁剪脏矩形到屏幕范围 */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    /* 清理DCache */
    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}


/* ============================================================================
 *                           缓存刷新函数
 * ============================================================================ */

/**
 * @brief  刷新整个图层的DCache
 * @note   在大量绘图操作后调用,确保所有修改立即可见
 */
void LCD_Refresh(uint8_t Layer)
{
    uint32_t *fb = LCD_GetFB(Layer);
    LCD_DCacheClean(fb, LCD_W * LCD_H * 4u);
}


/* ============================================================================
 *                           直线优化函数
 * ============================================================================ */

/**
 * @brief  绘制水平直线(优化版)
 */
void LCD_DrawHLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color)
{
    /* 边界检查 */
    if (Length == 0 || Y >= LCD_H || X >= LCD_W) return;

    /* 裁剪到屏幕范围 */
    uint32_t x_end = (uint32_t)X + Length;
    if (x_end > LCD_W) x_end = LCD_W;
    uint32_t actual_len = x_end - (uint32_t)X;

    /* 直接绘制矩形(1像素高) */
    LCD_DrawRect(Layer, X, Y, (uint16_t)actual_len, 1, Color);
}

/**
 * @brief  绘制垂直直线(优化版)
 */
void LCD_DrawVLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color)
{
    /* 边界检查 */
    if (Length == 0 || X >= LCD_W || Y >= LCD_H) return;

    /* 裁剪到屏幕范围 */
    uint32_t y_end = (uint32_t)Y + Length;
    if (y_end > LCD_H) y_end = LCD_H;
    uint32_t actual_len = y_end - (uint32_t)Y;

    /* 直接绘制矩形(1像素宽) */
    LCD_DrawRect(Layer, X, Y, 1, (uint16_t)actual_len, Color);
}


/* ============================================================================
 *                           矩形扩展函数
 * ============================================================================ */

/**
 * @brief  绘制实心矩形(语义清晰版本)
 */
void LCD_DrawRectFilled(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color)
{
    LCD_DrawRect(Layer, X, Y, W, H, Color);
}

/**
 * @brief  绘制矩形边框(空心矩形)
 */
void LCD_DrawRectOutline(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint8_t LineWidth, uint32_t Color)
{
    if (W == 0 || H == 0 || LineWidth == 0) return;

    /* 绘制四条边 */
    /* 上边 */
    LCD_DrawRect(Layer, X, Y, W, LineWidth, Color);

    /* 下边 */
    if (H > LineWidth) {
        LCD_DrawRect(Layer, X, (uint16_t)(Y + H - LineWidth), W, LineWidth, Color);
    }

    /* 左边 */
    if (H > 2 * LineWidth) {
        LCD_DrawRect(Layer, X, (uint16_t)(Y + LineWidth), LineWidth, (uint16_t)(H - 2 * LineWidth), Color);
    }

    /* 右边 */
    if (W > LineWidth && H > 2 * LineWidth) {
        LCD_DrawRect(Layer, (uint16_t)(X + W - LineWidth), (uint16_t)(Y + LineWidth),
                    LineWidth, (uint16_t)(H - 2 * LineWidth), Color);
    }
}


/* ============================================================================
 *                           三角形绘制函数实现
 * ============================================================================ */

/**
 * @brief  绘制空心三角形
 */
void LCD_DrawTriangle(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t LineWidth, uint32_t Color)
{
    /* 绘制三条边 */
    LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
    LCD_DrawLine(Layer, x1, y1, x2, y2, LineWidth, Color);
    LCD_DrawLine(Layer, x2, y2, x0, y0, LineWidth, Color);
}

/**
 * @brief  绘制实心三角形
 * @note   使用扫描线填充算法
 */
void LCD_DrawTriangleFilled(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t Color)
{
    /* 按Y坐标排序顶点: y0 <= y1 <= y2 */
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 > y2) { int t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }

    /* 记录脏矩形 */
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0, maxy = y2;

    /* 扫描线填充: 上半部分 (y0 到 y1) */
    if (y1 != y0) {
        for (int y = y0; y <= y1; y++) {
            /* 计算左右边界 */
            int xa = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
            int xb = x0 + (x2 - x0) * (y - y0) / (y2 - y0);

            if (xa > xb) { int t = xa; xa = xb; xb = t; }
            LCD_DrawRectFast(Layer, (uint16_t)xa, (uint16_t)y, (uint16_t)(xb - xa + 1), 1, Color);
        }
    }

    /* 扫描线填充: 下半部分 (y1 到 y2) */
    if (y2 != y1) {
        for (int y = y1; y <= y2; y++) {
            /* 计算左右边界 */
            int xa = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            int xb = x0 + (x2 - x0) * (y - y0) / (y2 - y0);

            if (xa > xb) { int t = xa; xa = xb; xb = t; }
            LCD_DrawRectFast(Layer, (uint16_t)xa, (uint16_t)y, (uint16_t)(xb - xa + 1), 1, Color);
        }
    }

    /* 清理DCache */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}


/* ============================================================================
 *                           多边形和折线函数实现
 * ============================================================================ */

/**
 * @brief  绘制连续折线(不闭合)
 */
void LCD_DrawPolyline(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color)
{
    if (count < 2 || points == NULL) return;

    /* 连续绘制线段 */
    for (uint16_t i = 0; i < count - 1; i++) {
        int x0 = points[i * 2];
        int y0 = points[i * 2 + 1];
        int x1 = points[(i + 1) * 2];
        int y1 = points[(i + 1) * 2 + 1];

        LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
    }
}

/**
 * @brief  绘制多边形边框(闭合)
 */
void LCD_DrawPolygon(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color)
{
    if (count < 3 || points == NULL) return;

    /* 绘制所有边 */
    for (uint16_t i = 0; i < count; i++) {
        int x0 = points[i * 2];
        int y0 = points[i * 2 + 1];
        int x1 = points[((i + 1) % count) * 2];
        int y1 = points[((i + 1) % count) * 2 + 1];

        LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
    }
}

/**
 * @brief  绘制实心多边形
 * @note   使用扫描线填充算法
 */
void LCD_DrawPolygonFilled(uint8_t Layer, const int16_t *points, uint16_t count, uint32_t Color)
{
    if (count < 3 || count > 100 || points == NULL) return;

    /* 计算边界框 */
    int minx = points[0], maxx = points[0];
    int miny = points[1], maxy = points[1];

    for (uint16_t i = 1; i < count; i++) {
        int x = points[i * 2];
        int y = points[i * 2 + 1];
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }

    /* 扫描线填充 */
    for (int y = miny; y <= maxy; y++) {
        /* 临时数组存储交点X坐标 */
        int16_t intersections[100];
        uint16_t n_intersect = 0;

        /* 计算扫描线与所有边的交点 */
        for (uint16_t i = 0; i < count; i++) {
            int x0 = points[i * 2];
            int y0 = points[i * 2 + 1];
            int x1 = points[((i + 1) % count) * 2];
            int y1 = points[((i + 1) % count) * 2 + 1];

            /* 确保y0 <= y1 */
            if (y0 > y1) {
                int t = y0; y0 = y1; y1 = t;
                t = x0; x0 = x1; x1 = t;
            }

            /* 检查扫描线是否与边相交 */
            if (y >= y0 && y < y1 && n_intersect < 100) {
                /* 计算交点X坐标 */
                int x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
                intersections[n_intersect++] = (int16_t)x;
            }
        }

        /* 对交点排序(冒泡排序) */
        for (uint16_t i = 0; i < n_intersect - 1; i++) {
            for (uint16_t j = 0; j < n_intersect - i - 1; j++) {
                if (intersections[j] > intersections[j + 1]) {
                    int16_t t = intersections[j];
                    intersections[j] = intersections[j + 1];
                    intersections[j + 1] = t;
                }
            }
        }

        /* 填充配对的交点之间的区域 */
        for (uint16_t i = 0; i < n_intersect; i += 2) {
            if (i + 1 < n_intersect) {
                int x_start = intersections[i];
                int x_end = intersections[i + 1];
                if (x_end > x_start) {
                    LCD_DrawRectFast(Layer, (uint16_t)x_start, (uint16_t)y,
                                   (uint16_t)(x_end - x_start), 1, Color);
                }
            }
        }
    }

    /* 清理DCache */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}


/* ============================================================================
 *                           椭圆绘制函数实现
 * ============================================================================ */

/**
 * @brief  绘制椭圆边框
 * @note   使用中点椭圆算法
 */
void LCD_DrawEllipse(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint8_t LineWidth, uint32_t Color)
{
    if (RadiusX == 0 || RadiusY == 0 || LineWidth == 0) return;

    int cx = (int)CenterX;
    int cy = (int)CenterY;
    int rx = (int)RadiusX;
    int ry = (int)RadiusY;

    /* 记录脏矩形 */
    int pad = (int)LineWidth;
    int minx = cx - rx - pad;
    int maxx = cx + rx + pad;
    int miny = cy - ry - pad;
    int maxy = cy + ry + pad;

    /* 中点椭圆算法 - 区域1 */
    int x = 0;
    int y = ry;
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int two_rx2 = 2 * rx2;
    int two_ry2 = 2 * ry2;
    int px = 0;
    int py = two_rx2 * y;
    int p = ry2 - (rx2 * ry) + (rx2 >> 2);

    /* 绘制辅助宏 */
    #define DRAW_ELLIPSE_POINT(px, py) \
        do { \
            if (LineWidth == 1) { \
                LCD_DrawPixelFast(Layer, px, py, Color); \
            } else { \
                int half = (int)LineWidth / 2; \
                LCD_DrawRectFast(Layer, (uint16_t)((px) - half), (uint16_t)((py) - half), \
                               LineWidth, LineWidth, Color); \
            } \
        } while(0)

    /* 区域1: px < py */
    while (px < py) {
        DRAW_ELLIPSE_POINT(cx + x, cy + y);
        DRAW_ELLIPSE_POINT(cx - x, cy + y);
        DRAW_ELLIPSE_POINT(cx + x, cy - y);
        DRAW_ELLIPSE_POINT(cx - x, cy - y);

        x++;
        px += two_ry2;

        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= two_rx2;
            p += ry2 + px - py;
        }
    }

    /* 区域2: px >= py */
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;

    while (y >= 0) {
        DRAW_ELLIPSE_POINT(cx + x, cy + y);
        DRAW_ELLIPSE_POINT(cx - x, cy + y);
        DRAW_ELLIPSE_POINT(cx + x, cy - y);
        DRAW_ELLIPSE_POINT(cx - x, cy - y);

        y--;
        py -= two_rx2;

        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += two_ry2;
            p += rx2 - py + px;
        }
    }

    #undef DRAW_ELLIPSE_POINT

    /* 清理DCache */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}

/**
 * @brief  绘制实心椭圆
 */
void LCD_DrawEllipseFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint32_t Color)
{
    if (RadiusX == 0 || RadiusY == 0) return;

    int cx = (int)CenterX;
    int cy = (int)CenterY;
    int rx = (int)RadiusX;
    int ry = (int)RadiusY;

    /* 记录脏矩形 */
    int minx = cx - rx;
    int maxx = cx + rx;
    int miny = cy - ry;
    int maxy = cy + ry;

    /* 中点椭圆算法 + 扫描线填充 */
    int x = 0;
    int y = ry;
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int two_rx2 = 2 * rx2;
    int two_ry2 = 2 * ry2;
    int px = 0;
    int py = two_rx2 * y;
    int p = ry2 - (rx2 * ry) + (rx2 >> 2);

    /* 绘制中心水平线 */
    LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)cy, (uint16_t)(2 * x + 1), 1, Color);

    /* 区域1 */
    while (px < py) {
        x++;
        px += two_ry2;

        if (p < 0) {
            p += ry2 + px;
        } else {
            /* 绘制水平扫描线 */
            LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy + y), (uint16_t)(2 * x + 1), 1, Color);
            LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy - y), (uint16_t)(2 * x + 1), 1, Color);

            y--;
            py -= two_rx2;
            p += ry2 + px - py;
        }
    }

    /* 区域2 */
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;

    while (y >= 0) {
        /* 绘制水平扫描线 */
        LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy + y), (uint16_t)(2 * x + 1), 1, Color);
        LCD_DrawRectFast(Layer, (uint16_t)(cx - x), (uint16_t)(cy - y), (uint16_t)(2 * x + 1), 1, Color);

        y--;
        py -= two_rx2;

        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += two_ry2;
            p += rx2 - py + px;
        }
    }

    /* 清理DCache */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}


/* ============================================================================
 *                           圆弧绘制函数实现
 * ============================================================================ */

#include <math.h>

/**
 * @brief  绘制圆弧
 */
void LCD_DrawArc(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius,
                 int16_t StartAngle, int16_t EndAngle, uint8_t LineWidth, uint32_t Color)
{
    if (Radius == 0 || LineWidth == 0) return;

    /* 角度标准化到0-360 */
    while (StartAngle < 0) StartAngle += 360;
    while (EndAngle < 0) EndAngle += 360;
    while (StartAngle >= 360) StartAngle -= 360;
    while (EndAngle >= 360) EndAngle -= 360;

    /* 记录脏矩形 */
    int pad = (int)LineWidth;
    int minx = (int)CenterX - (int)Radius - pad;
    int maxx = (int)CenterX + (int)Radius + pad;
    int miny = (int)CenterY - (int)Radius - pad;
    int maxy = (int)CenterY + (int)Radius + pad;

    /* 使用中点圆算法绘制圆弧 */
    int x = 0;
    int y = (int)Radius;
    int d = 3 - 2 * (int)Radius;
    int cx = (int)CenterX;
    int cy = (int)CenterY;

    /* 角度判断辅助函数 */
    #define IS_IN_ARC(px, py) \
        ({ \
            int dx = (px) - cx; \
            int dy = cy - (py); \
            float angle_deg = atan2f((float)dy, (float)dx) * 180.0f / 3.14159265f; \
            if (angle_deg < 0) angle_deg += 360.0f; \
            int in_arc = 0; \
            if (StartAngle <= EndAngle) { \
                in_arc = (angle_deg >= (float)StartAngle && angle_deg <= (float)EndAngle); \
            } else { \
                in_arc = (angle_deg >= (float)StartAngle || angle_deg <= (float)EndAngle); \
            } \
            in_arc; \
        })

    /* 绘制点宏 */
    #define DRAW_ARC_POINT(px, py) \
        do { \
            if (IS_IN_ARC(px, py)) { \
                if (LineWidth == 1) { \
                    LCD_DrawPixelFast(Layer, px, py, Color); \
                } else { \
                    int half = (int)LineWidth / 2; \
                    LCD_DrawRectFast(Layer, (uint16_t)((px) - half), (uint16_t)((py) - half), \
                                   LineWidth, LineWidth, Color); \
                } \
            } \
        } while(0)

    /* 绘制8个对称点(有角度判断) */
    DRAW_ARC_POINT(cx + x, cy + y);
    DRAW_ARC_POINT(cx - x, cy + y);
    DRAW_ARC_POINT(cx + x, cy - y);
    DRAW_ARC_POINT(cx - x, cy - y);
    DRAW_ARC_POINT(cx + y, cy + x);
    DRAW_ARC_POINT(cx - y, cy + x);
    DRAW_ARC_POINT(cx + y, cy - x);
    DRAW_ARC_POINT(cx - y, cy - x);

    while (x < y) {
        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;

        /* 绘制8个对称点 */
        DRAW_ARC_POINT(cx + x, cy + y);
        DRAW_ARC_POINT(cx - x, cy + y);
        DRAW_ARC_POINT(cx + x, cy - y);
        DRAW_ARC_POINT(cx - x, cy - y);
        DRAW_ARC_POINT(cx + y, cy + x);
        DRAW_ARC_POINT(cx - y, cy + x);
        DRAW_ARC_POINT(cx + y, cy - x);
        DRAW_ARC_POINT(cx - y, cy - x);
    }

    #undef IS_IN_ARC
    #undef DRAW_ARC_POINT

    /* 清理DCache */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)LCD_W) maxx = LCD_W - 1;
    if (maxy >= (int)LCD_H) maxy = LCD_H - 1;

    if (maxx >= minx && maxy >= miny) {
        uint32_t rect_w = (uint32_t)(maxx - minx + 1);
        uint32_t rect_h = (uint32_t)(maxy - miny + 1);
        uint32_t *fb = LCD_GetFB(Layer);
        uint32_t *start = fb + (uint32_t)miny * (uint32_t)LCD_W + (uint32_t)minx;
        uint32_t bytes = (((rect_h - 1u) * (uint32_t)LCD_W) + rect_w) * 4u;
        LCD_DCacheClean(start, bytes);
    }
}