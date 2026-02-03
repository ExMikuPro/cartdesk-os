/**
 * @file    lcd.h
 * @brief   LCD驱动头文件 - 基于STM32 LTDC控制器
 * @details 支持双层显示、硬件加速、ARGB8888格式
 */

#ifndef __LCD_H
#define __LCD_H

#include "main.h"

/* ============================================================================
 *                           显示配置常量
 * ============================================================================ */

#define MAX_LAYER_NUMBER       ((uint32_t)2)    // 最大图层数量
#define LTDC_ACTIVE_LAYER      ((uint32_t)1)    // 当前活动图层

#define LCD_W                  800              // 屏幕宽度(像素)
#define LCD_H                  480              // 屏幕高度(像素)

/* 帧缓冲区地址配置 (SDRAM) */
#ifndef LCD_FB0_ADDR
#define LCD_FB0_ADDR           0xD0000000u      // 图层0帧缓冲起始地址
#endif

#define FB_SIZE                (LCD_W * LCD_H * 4)  // 单帧缓冲区大小(字节)

#ifndef LCD_FB1_ADDR
#define LCD_FB1_ADDR           (LCD_FB0_ADDR + FB_SIZE)  // 图层1帧缓冲起始地址
#endif

/* 图层索引定义 */
#define LCD_LAYER0             0u               // 图层0(底层)
#define LCD_LAYER1             1u               // 图层1(顶层)


/* ============================================================================
 *                           颜色定义 (ARGB8888格式)
 * ============================================================================ */
/* 格式说明: 0xAARRGGBB (AA=透明度, RR=红, GG=绿, BB=蓝)
 * 以下所有颜色均为不透明 (AA = 0xFF)
 */

/* --- 基础颜色 --- */
#define LCD_COLOR_BLACK              0xFF000000u  // 黑色
#define LCD_COLOR_WHITE              0xFFFFFFFFu  // 白色
#define LCD_COLOR_RED                0xFFFF0000u  // 红色
#define LCD_COLOR_LIME               0xFF00FF00u  // 亮绿 / 荧光绿
#define LCD_COLOR_BLUE               0xFF0000FFu  // 蓝色
#define LCD_COLOR_YELLOW             0xFFFFFF00u  // 黄色
#define LCD_COLOR_CYAN               0xFF00FFFFu  // 青色 / 天蓝青
#define LCD_COLOR_MAGENTA            0xFFFF00FFu  // 品红 / 洋红

/* --- 灰度系列 --- */
#define LCD_COLOR_SILVER             0xFFC0C0C0u  // 银色 / 浅灰银
#define LCD_COLOR_GRAY               0xFF808080u  // 灰色

/* --- 深色调 --- */
#define LCD_COLOR_MAROON             0xFF800000u  // 栗色 / 暗红
#define LCD_COLOR_OLIVE              0xFF808000u  // 橄榄色 / 黄绿褐
#define LCD_COLOR_GREEN              0xFF008000u  // 绿色(深绿)
#define LCD_COLOR_PURPLE             0xFF800080u  // 紫色
#define LCD_COLOR_TEAL               0xFF008080u  // 蓝绿色 / 深青
#define LCD_COLOR_NAVY               0xFF000080u  // 海军蓝 / 深蓝

/* --- 暖色调 --- */
#define LCD_COLOR_ORANGE             0xFFFFA500u  // 橙色
#define LCD_COLOR_GOLD               0xFFFFD700u  // 金色
#define LCD_COLOR_PINK               0xFFFFC0CBu  // 粉色
#define LCD_COLOR_HOTPINK            0xFFFF69B4u  // 亮粉 / 桃红
#define LCD_COLOR_CORAL              0xFFFF7F50u  // 珊瑚色
#define LCD_COLOR_TOMATO             0xFFFF6347u  // 番茄红
#define LCD_COLOR_SALMON             0xFFFA8072u  // 鲑红 / 三文鱼色
#define LCD_COLOR_CRIMSON            0xFFDC143Cu  // 深红 / 绯红

/* --- 棕色系列 --- */
#define LCD_COLOR_BROWN              0xFFA52A2Au  // 棕色
#define LCD_COLOR_CHOCOLATE          0xFFD2691Eu  // 巧克力色(棕橙)
#define LCD_COLOR_SADDLEBROWN        0xFF8B4513u  // 马鞍棕 / 深棕
#define LCD_COLOR_TAN                0xFFD2B48Cu  // 黄褐色 / 浅棕
#define LCD_COLOR_WHEAT              0xFFF5DEB3u  // 麦色 / 浅米黄
#define LCD_COLOR_BEIGE              0xFFF5F5DCu  // 米色
#define LCD_COLOR_KHAKI              0xFFF0E68Cu  // 卡其色
#define LCD_COLOR_MOCCASIN           0xFFFFE4B5u  // 鹿皮色 / 浅橙米

/* --- 紫色系列 --- */
#define LCD_COLOR_INDIGO             0xFF4B0082u  // 靛蓝
#define LCD_COLOR_VIOLET             0xFFEE82EEu  // 紫罗兰色
#define LCD_COLOR_PLUM               0xFFDDA0DDu  // 李子紫 / 浅紫
#define LCD_COLOR_ORCHID             0xFFDA70D6u  // 兰花紫 / 玫紫
#define LCD_COLOR_MEDIUMPURPLE       0xFF9370DBu  // 中紫 / 中等紫
#define LCD_COLOR_SLATEBLUE          0xFF6A5ACDu  // 石板蓝(偏紫蓝)
#define LCD_COLOR_DARKSLATEBLUE      0xFF483D8Bu  // 深石板蓝(偏紫蓝)
#define LCD_COLOR_DARKMAGENTA        0xFF8B008Bu  // 深品红 / 深洋红

/* --- 蓝色系列 --- */
#define LCD_COLOR_SKYBLUE            0xFF87CEEBu  // 天空蓝
#define LCD_COLOR_DEEPSKYBLUE        0xFF00BFFFu  // 深天蓝
#define LCD_COLOR_DODGERBLUE         0xFF1E90FFu  // 道奇蓝 / 亮蓝
#define LCD_COLOR_STEELBLUE          0xFF4682B4u  // 钢蓝
#define LCD_COLOR_ROYALBLUE          0xFF4169E1u  // 宝蓝 / 皇家蓝
#define LCD_COLOR_LIGHTBLUE          0xFFADD8E6u  // 浅蓝
#define LCD_COLOR_LIGHTCYAN          0xFFE0FFFFu  // 浅青 / 淡青
#define LCD_COLOR_AQUAMARINE         0xFF7FFFD4u  // 海蓝宝 / 碧绿

/* --- 青绿色系列 --- */
#define LCD_COLOR_TURQUOISE          0xFF40E0D0u  // 绿松石色
#define LCD_COLOR_MEDIUMTURQUOISE    0xFF48D1CCu  // 中绿松石
#define LCD_COLOR_DARKTURQUOISE      0xFF00CED1u  // 深绿松石
#define LCD_COLOR_PALETURQUOISE      0xFFAFEEEEu  // 浅绿松石 / 淡青绿
#define LCD_COLOR_LIGHTSEAGREEN      0xFF20B2AAu  // 浅海绿色(青绿)
#define LCD_COLOR_SEAGREEN           0xFF2E8B57u  // 海绿色
#define LCD_COLOR_MEDIUMSEAGREEN     0xFF3CB371u  // 中海绿色
#define LCD_COLOR_SPRINGGREEN        0xFF00FF7Fu  // 春绿色 / 嫩绿

/* --- 绿色系列 --- */
#define LCD_COLOR_LAWNGREEN          0xFF7CFC00u  // 草坪绿 / 鲜绿
#define LCD_COLOR_CHARTREUSE         0xFF7FFF00u  // 黄绿色 / 夏特勒兹绿
#define LCD_COLOR_GREENYELLOW        0xFFADFF2Fu  // 绿黄
#define LCD_COLOR_YELLOWGREEN        0xFF9ACD32u  // 黄绿
#define LCD_COLOR_OLIVEDRAB          0xFF6B8E23u  // 橄榄褐绿
#define LCD_COLOR_DARKGREEN          0xFF006400u  // 深绿
#define LCD_COLOR_DARKOLIVEGREEN     0xFF556B2Fu  // 深橄榄绿
#define LCD_COLOR_FORESTGREEN        0xFF228B22u  // 森林绿


/* ============================================================================
 *                           图层控制函数
 * ============================================================================ */

/**
 * @brief  选择当前操作的图层
 * @param  LayerIndex: 图层索引 (LCD_LAYER0 或 LCD_LAYER1)
 */
void LCD_Select(uint32_t LayerIndex);

/**
 * @brief  获取当前图层的Y尺寸(高度)
 * @return 图层高度(像素)
 */
uint32_t LCD_GetYSize(void);

/**
 * @brief  获取当前图层的X尺寸(宽度)
 * @return 图层宽度(像素)
 */
uint32_t LCD_GetXSize(void);

/**
 * @brief  打开LCD显示(使能LTDC和背光)
 */
void LCD_DisplayON(void);

/**
 * @brief  关闭LCD显示(禁用LTDC和背光)
 */
void LCD_DisplayOFF(void);

/**
 * @brief  设置图层透明度
 * @param  LayerIndex: 图层索引
 * @param  Transparency: 透明度值 (0-255, 0=完全透明, 255=完全不透明)
 */
void LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency);

/**
 * @brief  设置图层可见性
 * @param  LayerIndex: 图层索引
 * @param  Status: 0=隐藏, 非0=显示
 */
void LCD_SetLayerVisible(uint32_t LayerIndex, uint8_t Status);


/* ============================================================================
 *                           帧缓冲区访问函数
 * ============================================================================ */

/**
 * @brief  获取指定图层的帧缓冲区地址
 * @param  Layer: 图层索引 (LCD_LAYER0 或 LCD_LAYER1)
 * @return 帧缓冲区起始地址指针
 */
uint32_t* LCD_GetFB(uint8_t Layer);


/* ============================================================================
 *                           基础绘图函数
 * ============================================================================ */

/**
 * @brief  清空指定图层(填充黑色)
 * @param  Layer: 图层索引
 */
void LCD_Clear(uint8_t Layer);

/**
 * @brief  用指定颜色填充整个图层
 * @param  Layer: 图层索引
 * @param  Color: 填充颜色 (ARGB8888格式)
 */
void LCD_Fill(uint8_t Layer, uint32_t Color);

/**
 * @brief  绘制实心矩形
 * @param  Layer: 图层索引
 * @param  X: 起始X坐标(左上角)
 * @param  Y: 起始Y坐标(左上角)
 * @param  W: 宽度(像素)
 * @param  H: 高度(像素)
 * @param  Color: 填充颜色
 */
void LCD_DrawRect(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color);

/**
 * @brief  绘制实心矩形(语义清晰版本)
 * @param  Layer: 图层索引
 * @param  X: 起始X坐标(左上角)
 * @param  Y: 起始Y坐标(左上角)
 * @param  W: 宽度(像素)
 * @param  H: 高度(像素)
 * @param  Color: 填充颜色
 * @note   功能同LCD_DrawRect,仅为语义明确
 */
void LCD_DrawRectFilled(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color);

/**
 * @brief  绘制矩形边框(空心矩形)
 * @param  Layer: 图层索引
 * @param  X: 起始X坐标(左上角)
 * @param  Y: 起始Y坐标(左上角)
 * @param  W: 宽度(像素)
 * @param  H: 高度(像素)
 * @param  LineWidth: 边框线宽(像素)
 * @param  Color: 边框颜色
 */
void LCD_DrawRectOutline(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint8_t LineWidth, uint32_t Color);

/**
 * @brief  绘制方形点(笔刷)
 * @param  Layer: 图层索引
 * @param  X: 中心X坐标
 * @param  Y: 中心Y坐标
 * @param  Size: 点的尺寸(边长, 像素)
 * @param  Color: 颜色
 */
void LCD_DrawPoint(uint8_t Layer, uint16_t X, uint16_t Y, uint8_t Size, uint32_t Color);

/**
 * @brief  绘制单个像素
 * @param  Layer: 图层索引
 * @param  X: X坐标
 * @param  Y: Y坐标
 * @param  Color: 颜色
 */
void LCD_DrawPixel(uint8_t Layer, uint16_t X, uint16_t Y, uint32_t Color);

/**
 * @brief  绘制直线(Bresenham算法)
 * @param  Layer: 图层索引
 * @param  x0: 起点X坐标
 * @param  y0: 起点Y坐标
 * @param  x1: 终点X坐标
 * @param  y1: 终点Y坐标
 * @param  Size: 线宽(像素, 1=单像素线)
 * @param  Color: 颜色
 */
void LCD_DrawLine(uint8_t Layer, int x0, int y0, int x1, int y1, uint8_t Size, uint32_t Color);

/**
 * @brief  绘制水平直线(优化版)
 * @param  Layer: 图层索引
 * @param  X: 起点X坐标
 * @param  Y: Y坐标
 * @param  Length: 长度(像素)
 * @param  Color: 颜色
 * @note   比通用DrawLine更快,常用于表格、网格
 */
void LCD_DrawHLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color);

/**
 * @brief  绘制垂直直线(优化版)
 * @param  Layer: 图层索引
 * @param  X: X坐标
 * @param  Y: 起点Y坐标
 * @param  Length: 长度(像素)
 * @param  Color: 颜色
 * @note   比通用DrawLine更快,常用于表格、网格
 */
void LCD_DrawVLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color);


/* ============================================================================
 *                           圆形绘制函数
 * ============================================================================ */

/**
 * @brief  绘制实心圆
 * @param  Layer: 图层索引
 * @param  CenterX: 圆心X坐标
 * @param  CenterY: 圆心Y坐标
 * @param  Radius: 半径(像素)
 * @param  Color: 填充颜色
 * @note   使用中点圆算法优化,自动处理DCache一致性
 */
void LCD_DrawCircleFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint32_t Color);

/**
 * @brief  绘制空心圆
 * @param  Layer: 图层索引
 * @param  CenterX: 圆心X坐标
 * @param  CenterY: 圆心Y坐标
 * @param  Radius: 半径(像素)
 * @param  LineWidth: 线宽(像素, 1=单像素线)
 * @param  Color: 边框颜色
 * @note   使用中点圆算法,支持粗线宽绘制
 */
void LCD_DrawCircle(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint8_t LineWidth, uint32_t Color);


/* ============================================================================
 *                           三角形绘制函数
 * ============================================================================ */

/**
 * @brief  绘制空心三角形
 * @param  Layer: 图层索引
 * @param  x0,y0: 第一个顶点坐标
 * @param  x1,y1: 第二个顶点坐标
 * @param  x2,y2: 第三个顶点坐标
 * @param  LineWidth: 边框线宽(像素)
 * @param  Color: 边框颜色
 */
void LCD_DrawTriangle(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t LineWidth, uint32_t Color);

/**
 * @brief  绘制实心三角形
 * @param  Layer: 图层索引
 * @param  x0,y0: 第一个顶点坐标
 * @param  x1,y1: 第二个顶点坐标
 * @param  x2,y2: 第三个顶点坐标
 * @param  Color: 填充颜色
 * @note   使用扫描线填充算法
 */
void LCD_DrawTriangleFilled(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t Color);


/* ============================================================================
 *                           多边形和折线绘制函数
 * ============================================================================ */

/**
 * @brief  绘制连续折线(不闭合)
 * @param  Layer: 图层索引
 * @param  points: 顶点数组,格式为{x0,y0, x1,y1, x2,y2, ...}
 * @param  count: 顶点数量
 * @param  LineWidth: 线宽(像素)
 * @param  Color: 线条颜色
 * @note   适用于示波器波形、曲线图、路径绘制
 */
void LCD_DrawPolyline(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color);

/**
 * @brief  绘制多边形边框(闭合)
 * @param  Layer: 图层索引
 * @param  points: 顶点数组,格式为{x0,y0, x1,y1, x2,y2, ...}
 * @param  count: 顶点数量
 * @param  LineWidth: 边框线宽(像素)
 * @param  Color: 边框颜色
 */
void LCD_DrawPolygon(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color);

/**
 * @brief  绘制实心多边形
 * @param  Layer: 图层索引
 * @param  points: 顶点数组,格式为{x0,y0, x1,y1, x2,y2, ...}
 * @param  count: 顶点数量(3-100)
 * @param  Color: 填充颜色
 * @note   使用扫描线填充算法,适用于凸多边形和凹多边形
 */
void LCD_DrawPolygonFilled(uint8_t Layer, const int16_t *points, uint16_t count, uint32_t Color);


/* ============================================================================
 *                           椭圆绘制函数
 * ============================================================================ */

/**
 * @brief  绘制椭圆边框
 * @param  Layer: 图层索引
 * @param  CenterX: 中心X坐标
 * @param  CenterY: 中心Y坐标
 * @param  RadiusX: X轴半径(水平)
 * @param  RadiusY: Y轴半径(垂直)
 * @param  LineWidth: 边框线宽(像素)
 * @param  Color: 边框颜色
 * @note   使用中点椭圆算法
 */
void LCD_DrawEllipse(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint8_t LineWidth, uint32_t Color);

/**
 * @brief  绘制实心椭圆
 * @param  Layer: 图层索引
 * @param  CenterX: 中心X坐标
 * @param  CenterY: 中心Y坐标
 * @param  RadiusX: X轴半径(水平)
 * @param  RadiusY: Y轴半径(垂直)
 * @param  Color: 填充颜色
 * @note   使用扫描线填充优化
 */
void LCD_DrawEllipseFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint32_t Color);


/* ============================================================================
 *                           圆弧绘制函数
 * ============================================================================ */

/**
 * @brief  绘制圆弧
 * @param  Layer: 图层索引
 * @param  CenterX: 圆心X坐标
 * @param  CenterY: 圆心Y坐标
 * @param  Radius: 半径(像素)
 * @param  StartAngle: 起始角度(度数, 0°=3点钟方向, 顺时针)
 * @param  EndAngle: 结束角度(度数)
 * @param  LineWidth: 线宽(像素)
 * @param  Color: 颜色
 * @note   适用于仪表盘、进度环、扇形图表
 * @example LCD_DrawArc(0, 400, 240, 100, 0, 90, 5, RED); // 绘制90度圆弧
 */
void LCD_DrawArc(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius,
                 int16_t StartAngle, int16_t EndAngle, uint8_t LineWidth, uint32_t Color);


/* ============================================================================
 *                           缓存管理函数
 * ============================================================================ */

/**
 * @brief  刷新整个图层的DCache(强制同步到SDRAM)
 * @param  Layer: 图层索引
 * @note   在大量绘图后调用,确保所有数据可见
 */
void LCD_Refresh(uint8_t Layer);


#endif /* __LCD_H */