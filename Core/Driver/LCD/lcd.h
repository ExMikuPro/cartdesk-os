/**
 * @file    lcd.h
 * @brief   LCD 驱动头文件（LTDC + SDRAM Framebuffer，ARGB8888）
 *          【已修复】统一back buffer绘制 + VBlank稳定swap + int32坐标支持
 *
 * 坐标系：
 * - 原点 (0,0) 在左上角
 * - X 向右增大，Y 向下增大
 * - 有效范围：X ∈ [0, LCD_W-1]，Y ∈ [0, LCD_H-1]
 *
 * 像素格式：
 * - ARGB8888：0xAARRGGBB
 *   - A：Alpha（0=完全透明，255=完全不透明）
 *   - R/G/B：颜色分量
 *
 * 图层（Layer）：
 * - 通常 Layer0 用于背景，Layer1 用于 UI/覆盖层（具体以你的 LTDC 配置为准）
 *
 * Framebuffer（帧缓冲）：
 * - 默认放在外部 SDRAM（本工程基址 LCD_FB0_ADDR）
 * - 单帧大小：LCD_W * LCD_H * 4 字节
 *
 * 双缓冲/VSync 说明（如果你实现了）：
 * - 推荐只在 back buffer 上绘制，绘制完成后调用 LCD_Refresh(layer)
 * - 由 VBlank/VSync 时刻切换到 front，避免撕裂
 *
 * 注意：
 * - 硬件初始化（MX_LTDC_Init / MX_DMA2D_Init 等）应由 CubeMX 生成代码负责
 * - 本驱动建议只做"绘制/提交/缓存管理"等运行期逻辑
 */

#ifndef __LCD_H
#define __LCD_H

#include "../../Inc/main.h"

#ifdef __cplusplus
extern "C" {

#endif

/* ============================================================================
 *                           基础配置 / 常量
 * ============================================================================ */

/** 最大图层数量（与你的 LTDC 配置一致；常见为 2 层） */
#define MAX_LAYER_NUMBER       ((uint32_t)2)

/**
 * 当前活动层（legacy/兼容用）
 * - 你的实现可能会用它作为"默认绘制层"
 * - 若你所有 API 都显式传 Layer 参数，则该宏仅作为参考
 */
#define LTDC_ACTIVE_LAYER      ((uint32_t)1)

/** 屏幕宽度（像素） */
#define LCD_W                  800

/** 屏幕高度（像素） */
#define LCD_H                  480

/** 外部 SDRAM framebuffer 基址（字节地址） */
#define LCD_FB0_ADDR           0xD0000000u

/** 单帧大小（字节）：W * H * 4（ARGB8888） */
#define FB_SIZE                (LCD_W * LCD_H * 4)

/**
 * 第二个 framebuffer 基址（字节地址）
 * - 常见用法：
 *   - 单缓冲：Layer1 的帧缓冲
 *   - 或双缓冲：某层的第二帧（取决于你在 lcd.c 里的地址规划）
 */
#define LCD_FB1_ADDR           (LCD_FB0_ADDR + FB_SIZE)

/** Layer 0 索引（背景层） */
#define LCD_LAYER0             0u

/** Layer 1 索引（覆盖层/UI 层） */
#define LCD_LAYER1             1u


/* ============================================================================
 *                           颜色定义（ARGB8888）
 * ============================================================================ */
/**
 * 颜色值格式：0xAARRGGBB
 * - A：Alpha（0~255）
 * - R/G/B：颜色分量（0~255）
 *
 * 注意：
 * - 如果你的 LTDC Layer 配置使用了"常量 Alpha"，像素 Alpha 的效果可能与期望不同
 */

#define LCD_COLOR_BLACK              0xFF000000u
#define LCD_COLOR_WHITE              0xFFFFFFFFu
#define LCD_COLOR_RED                0xFFFF0000u
#define LCD_COLOR_LIME               0xFF00FF00u
#define LCD_COLOR_BLUE               0xFF0000FFu
#define LCD_COLOR_YELLOW             0xFFFFFF00u
#define LCD_COLOR_CYAN               0xFF00FFFFu
#define LCD_COLOR_MAGENTA            0xFFFF00FFu
#define LCD_COLOR_SILVER             0xFFC0C0C0u
#define LCD_COLOR_GRAY               0xFF808080u
#define LCD_COLOR_MAROON             0xFF800000u
#define LCD_COLOR_OLIVE              0xFF808000u
#define LCD_COLOR_GREEN              0xFF008000u
#define LCD_COLOR_PURPLE             0xFF800080u
#define LCD_COLOR_TEAL               0xFF008080u
#define LCD_COLOR_NAVY               0xFF000080u
#define LCD_COLOR_ORANGE             0xFFFFA500u
#define LCD_COLOR_GOLD               0xFFFFD700u
#define LCD_COLOR_PINK               0xFFFFC0CBu
#define LCD_COLOR_HOTPINK            0xFFFF69B4u
#define LCD_COLOR_CORAL              0xFFFF7F50u
#define LCD_COLOR_TOMATO             0xFFFF6347u
#define LCD_COLOR_SALMON             0xFFFA8072u
#define LCD_COLOR_CRIMSON            0xFFDC143Cu
#define LCD_COLOR_BROWN              0xFFA52A2Au
#define LCD_COLOR_CHOCOLATE          0xFFD2691Eu
#define LCD_COLOR_SADDLEBROWN        0xFF8B4513u
#define LCD_COLOR_TAN                0xFFD2B48Cu
#define LCD_COLOR_WHEAT              0xFFF5DEB3u
#define LCD_COLOR_BEIGE              0xFFF5F5DCu
#define LCD_COLOR_KHAKI              0xFFF0E68Cu
#define LCD_COLOR_MOCCASIN           0xFFFFE4B5u
#define LCD_COLOR_INDIGO             0xFF4B0082u
#define LCD_COLOR_VIOLET             0xFFEE82EEu
#define LCD_COLOR_PLUM               0xFFDDA0DDu
#define LCD_COLOR_ORCHID             0xFFDA70D6u
#define LCD_COLOR_MEDIUMPURPLE       0xFF9370DBu
#define LCD_COLOR_SLATEBLUE          0xFF6A5ACDu
#define LCD_COLOR_DARKSLATEBLUE      0xFF483D8Bu
#define LCD_COLOR_DARKMAGENTA        0xFF8B008Bu
#define LCD_COLOR_SKYBLUE            0xFF87CEEBu
#define LCD_COLOR_DEEPSKYBLUE        0xFF00BFFFu
#define LCD_COLOR_DODGERBLUE         0xFF1E90FFu
#define LCD_COLOR_STEELBLUE          0xFF4682B4u
#define LCD_COLOR_ROYALBLUE          0xFF4169E1u
#define LCD_COLOR_LIGHTBLUE          0xFFADD8E6u
#define LCD_COLOR_LIGHTCYAN          0xFFE0FFFFu
#define LCD_COLOR_AQUAMARINE         0xFF7FFFD4u
#define LCD_COLOR_TURQUOISE          0xFF40E0D0u
#define LCD_COLOR_MEDIUMTURQUOISE    0xFF48D1CCu
#define LCD_COLOR_DARKTURQUOISE      0xFF00CED1u
#define LCD_COLOR_PALETURQUOISE      0xFFAFEEEEu
#define LCD_COLOR_LIGHTSEAGREEN      0xFF20B2AAu
#define LCD_COLOR_SEAGREEN           0xFF2E8B57u
#define LCD_COLOR_MEDIUMSEAGREEN     0xFF3CB371u
#define LCD_COLOR_SPRINGGREEN        0xFF00FF7Fu
#define LCD_COLOR_LAWNGREEN          0xFF7CFC00u
#define LCD_COLOR_CHARTREUSE         0xFF7FFF00u
#define LCD_COLOR_GREENYELLOW        0xFFADFF2Fu
#define LCD_COLOR_YELLOWGREEN        0xFF9ACD32u
#define LCD_COLOR_OLIVEDRAB          0xFF6B8E23u
#define LCD_COLOR_DARKGREEN          0xFF006400u
#define LCD_COLOR_DARKOLIVEGREEN     0xFF556B2Fu
#define LCD_COLOR_FORESTGREEN        0xFF228B22u


/* ============================================================================
 *                           显示/图层控制
 * ============================================================================ */

/**
 * @brief 选择当前活动层（用于兼容旧接口/全局默认层）。
 * @param LayerIndex 图层索引（LCD_LAYER0 / LCD_LAYER1）。
 * @note  如果你的绘制 API 都显式传入 Layer 参数，则可不使用该函数。
 */
void LCD_Select(uint32_t LayerIndex);

/**
 * @brief 获取屏幕高度（像素）。
 * @return LCD_H
 */
uint32_t LCD_GetYSize(void);

/**
 * @brief 获取屏幕宽度（像素）。
 * @return LCD_W
 */
uint32_t LCD_GetXSize(void);

/**
 * @brief 打开显示（通常包含：使能 LTDC + 打开背光）。
 * @note  背光 GPIO/极性以你的工程配置为准。
 */
void LCD_DisplayON(void);

/**
 * @brief 关闭显示（通常包含：关闭背光 + 禁用 LTDC）。
 */
void LCD_DisplayOFF(void);

/**
 * @brief 设置某个图层的"常量透明度"（Constant Alpha）。
 * @param LayerIndex 图层索引（LCD_LAYER0 / LCD_LAYER1）。
 * @param Transparency 透明度 0~255：
 *        - 0：完全透明
 *        - 255：完全不透明
 * @note  该透明度与像素自身 ARGB 的 A 值如何组合，取决于 LTDC 的混合配置。
 */
void LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency);

/**
 * @brief 设置图层可见性（显示/隐藏）。
 * @param LayerIndex 图层索引（LCD_LAYER0 / LCD_LAYER1）。
 * @param Status 0=隐藏，非0=显示。
 * @note  许多实现会在内部请求一次 LTDC Reload（建议 VBlank Reload）。
 */
void LCD_SetLayerVisible(uint32_t LayerIndex, uint8_t Status);


/* ============================================================================
 *                           Framebuffer 访问
 * ============================================================================ */

/**
 * @brief 获取某层 framebuffer 指针（通常是"当前显示的 front buffer"）。
 * @param Layer 图层索引（0/1）。
 * @return 指向 ARGB8888 framebuffer 的 uint32_t*；每个元素=1像素（0xAARRGGBB）。
 * @warning 若你启用了双缓冲，请避免直接写 front buffer（会产生撕裂风险）。
 */
uint32_t *LCD_GetFB(uint8_t Layer);

/**
 * @brief 获取某层"绘制用 framebuffer"（通常是 back buffer）。
 * @param Layer 图层索引（0/1）。
 * @return 指向 ARGB8888 framebuffer 的 uint32_t*。
 * @note  若当前工程未实现双缓冲，此函数可能与 LCD_GetFB 返回相同地址（以 lcd.c 实现为准）。
 */
uint32_t *LCD_GetDrawFB(uint8_t Layer);


/* ============================================================================
 *                           基础绘图 API（常用于 UI/动画）
 * ============================================================================ */

/**
 * @brief 清空指定图层（通常填充为黑色或透明，取决于实现）。
 * @param Layer 图层索引（0/1）。
 * @note  若 framebuffer 是 cacheable，函数内部可能会进行 DCache clean（或仅标记，最终在 LCD_Refresh 做）。
 */
void LCD_Clear(uint8_t Layer);

/**
 * @brief 用指定颜色填充整个图层。
 * @param Layer 图层索引（0/1）。
 * @param Color 颜色（ARGB8888：0xAARRGGBB）。
 */
void LCD_Fill(uint8_t Layer, uint32_t Color);

/**
 * @brief 绘制矩形（实心填充矩形）。
 * @param Layer 图层索引（0/1）。
 * @param X 左上角 X（像素）。
 * @param Y 左上角 Y（像素）。
 * @param W 宽度（像素），W>0。
 * @param H 高度（像素），H>0。
 * @param Color 颜色（ARGB8888）。
 * @note  实现可能使用 CPU 填充或 DMA2D 加速（以 lcd.c 实现为准）。
 */
void LCD_DrawRect(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color);

/**
 * @brief 绘制实心矩形（与 LCD_DrawRect 语义等价的"更直观命名"版本）。
 * @param Layer 图层索引（0/1）。
 * @param X 左上角 X（像素）。
 * @param Y 左上角 Y（像素）。
 * @param W 宽度（像素）。
 * @param H 高度（像素）。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawRectFilled(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color);

/**
 * @brief 绘制带线宽的矩形外框。
 * @param Layer 图层索引（0/1）。
 * @param X 左上角 X（像素）。
 * @param Y 左上角 Y（像素）。
 * @param W 宽度（像素），W>0。
 * @param H 高度（像素），H>0。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawRectOutline(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint8_t LineWidth,
                         uint32_t Color);

/**
 * @brief 绘制点（方形笔刷）。
 * @param Layer 图层索引（0/1）。
 * @param X 点中心/左上角 X（像素，取决于实现；一般按左上角填充 Size×Size）。
 * @param Y 点中心/左上角 Y（像素）。
 * @param Size 点的尺寸（像素），Size>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawPoint(uint8_t Layer, uint16_t X, uint16_t Y, uint8_t Size, uint32_t Color);

/**
 * @brief 绘制单个像素。
 * @param Layer 图层索引（0/1）。
 * @param X 像素 X（像素坐标）。
 * @param Y 像素 Y（像素坐标）。
 * @param Color 颜色（ARGB8888）。
 * @note  若开启 DCache，为了让 LTDC 立即读到新像素，通常需要对对应 cache line 做 clean。
 *        频繁单像素绘制会很慢，建议批量绘制后统一 LCD_Refresh。
 */
void LCD_DrawPixel(uint8_t Layer, uint16_t X, uint16_t Y, uint32_t Color);

/**
 * @brief 绘制直线（任意方向）。
 * @param Layer 图层索引（0/1）。
 * @param x0 起点 X（像素）。
 * @param y0 起点 Y（像素）。
 * @param x1 终点 X（像素）。
 * @param y1 终点 Y（像素）。
 * @param Size 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawLine(uint8_t Layer, int x0, int y0, int x1, int y1, uint8_t Size, uint32_t Color);

/**
 * @brief 绘制水平线。
 * @param Layer 图层索引（0/1）。
 * @param X 起点 X（像素）。
 * @param Y 固定 Y（像素）。
 * @param Length 长度（像素），Length>0。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawHLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color);

/**
 * @brief 绘制垂直线。
 * @param Layer 图层索引（0/1）。
 * @param X 固定 X（像素）。
 * @param Y 起点 Y（像素）。
 * @param Length 长度（像素），Length>0。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawVLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color);


/* ============================================================================
 *                    【新增】int32坐标API - 支持边界裁剪
 * ============================================================================ */
/**
 * @brief 绘制带线宽的矩形外框（int32坐标版，自动裁剪到屏幕范围）
 * @param Layer 图层索引（0/1）
 * @param x 左上角 X（可以<0或>LCD_W，会自动裁剪）
 * @param y 左上角 Y（可以<0或>LCD_H，会自动裁剪）
 * @param w 宽度（像素）
 * @param h 高度（像素）
 * @param LineWidth 线宽（像素），>=1
 * @param Color 颜色（ARGB8888）
 *
 * 用途：解决上层传入"半个图标露出屏幕边缘"时uint16_t wrap导致的异常
 * 示例：LCD_DrawRectOutlineI32(1, -10, 50, 60, 80, 2, 0xFFFF0000)
 *       会正确绘制x=0开始、宽度50的可见部分
 */
void LCD_DrawRectOutlineI32(uint8_t Layer, int x, int y, int w, int h, int lineWidth, uint32_t Color);

/**
 * @brief 绘制实心矩形（int32坐标版，自动裁剪到屏幕范围）
 * @param Layer 图层索引（0/1）
 * @param x 左上角 X（可以<0或>LCD_W）
 * @param y 左上角 Y（可以<0或>LCD_H）
 * @param w 宽度（像素）
 * @param h 高度（像素）
 * @param Color 颜色（ARGB8888）
 */
void LCD_DrawRectFilledI32(uint8_t Layer, int x, int y, int w, int h, uint32_t Color);


/* ============================================================================
 *                           圆/三角/多边形/椭圆/弧线
 * ============================================================================ */

/**
 * @brief 绘制实心圆。
 * @param Layer 图层索引（0/1）。
 * @param CenterX 圆心 X（像素）。
 * @param CenterY 圆心 Y（像素）。
 * @param Radius 半径（像素），>0。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawCircleFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint32_t Color);

/**
 * @brief 绘制空心圆（支持线宽）。
 * @param Layer 图层索引（0/1）。
 * @param CenterX 圆心 X（像素）。
 * @param CenterY 圆心 Y（像素）。
 * @param Radius 半径（像素），>0。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawCircle(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint8_t LineWidth,
                    uint32_t Color);

/**
 * @brief 绘制三角形外框（3条边）。
 * @param Layer 图层索引（0/1）。
 * @param x0 顶点0 X（像素）。
 * @param y0 顶点0 Y（像素）。
 * @param x1 顶点1 X（像素）。
 * @param y1 顶点1 Y（像素）。
 * @param x2 顶点2 X（像素）。
 * @param y2 顶点2 Y（像素）。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawTriangle(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t LineWidth, uint32_t Color);

/**
 * @brief 绘制实心三角形（填充）。
 * @param Layer 图层索引（0/1）。
 * @param x0,y0 顶点0（像素）。
 * @param x1,y1 顶点1（像素）。
 * @param x2,y2 顶点2（像素）。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawTriangleFilled(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t Color);

/**
 * @brief 绘制折线（点序列）。
 * @param Layer 图层索引（0/1）。
 * @param points 点数组：按 [x0,y0,x1,y1,...] 组织（int16_t）。
 * @param count 点的数量（>=2）。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawPolyline(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color);

/**
 * @brief 绘制多边形外框（闭合）。
 * @param Layer 图层索引（0/1）。
 * @param points 点数组：按 [x0,y0,x1,y1,...] 组织（int16_t）。
 * @param count 点数量（>=3）。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawPolygon(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color);

/**
 * @brief 绘制实心多边形（闭合填充）。
 * @param Layer 图层索引（0/1）。
 * @param points 点数组：按 [x0,y0,x1,y1,...] 组织（int16_t）。
 * @param count 点数量（>=3）。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawPolygonFilled(uint8_t Layer, const int16_t *points, uint16_t count, uint32_t Color);

/**
 * @brief 绘制椭圆外框。
 * @param Layer 图层索引（0/1）。
 * @param CenterX 椭圆中心 X（像素）。
 * @param CenterY 椭圆中心 Y（像素）。
 * @param RadiusX X 半径（像素）。
 * @param RadiusY Y 半径（像素）。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawEllipse(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY,
                     uint8_t LineWidth, uint32_t Color);

/**
 * @brief 绘制实心椭圆。
 * @param Layer 图层索引（0/1）。
 * @param CenterX 椭圆中心 X（像素）。
 * @param CenterY 椭圆中心 Y（像素）。
 * @param RadiusX X 半径（像素）。
 * @param RadiusY Y 半径（像素）。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawEllipseFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY,
                           uint32_t Color);

/**
 * @brief 绘制圆弧外框。
 * @param Layer 图层索引（0/1）。
 * @param CenterX 圆心 X（像素）。
 * @param CenterY 圆心 Y（像素）。
 * @param Radius 半径（像素）。
 * @param StartAngle 起始角（单位/方向以实现为准，常见为"度"）。
 * @param EndAngle 结束角（同上）。
 * @param LineWidth 线宽（像素），>=1。
 * @param Color 颜色（ARGB8888）。
 */
void LCD_DrawArc(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius,
                 int16_t StartAngle, int16_t EndAngle, uint8_t LineWidth, uint32_t Color);


/* ============================================================================
 *                           刷新 / 双缓冲
 * ============================================================================ */

/**
 * @brief 提交刷新某层内容（常见用途：DCache clean / 双缓冲提交）。
 * @param Layer 图层索引（0/1）。
 * @note  若 framebuffer 是 cacheable：此函数通常会对相应范围执行 DCache clean，保证 LTDC 读到最新数据。
 * @note  若你实现了双缓冲：此函数也可用于"提交 back -> 请求 VBlank 翻页"。
 *        具体行为以 lcd.c 的实现为准。
 */
void LCD_Refresh(uint8_t Layer);

/**
 * @brief 双缓冲初始化（逻辑初始化，不应包含 MX_LTDC_Init/MX_DMA2D_Init 等硬件初始化）。
 * @note  典型用途：
 *        - 设置每层 front/back framebuffer 地址
 *        - 清空 pending_swap / dirty 标志
 *        - 【已修复】确保Layer1的front/back两块buffer都初始化为透明，避免swap时"下半闪"
 *        - 可选：启用/设置 LTDC LineEvent（用于 VBlank 时刻切换 CFBAR）
 *        具体内容以你的 lcd.c 实现为准。
 */
void LCD_DoubleBufferInit(void);

/**
 * @brief 获取VBlank计数（用于上层按帧节流渲染）
 * @return 从LCD_DoubleBufferInit开始累计的VBlank次数
 * @note 每次VBlank回调时+1，可用于实现"每帧只渲染一次"逻辑
 */
uint32_t LCD_GetVBlankCount(void);

/**
* @brief 检查某层是否有待处理的swap请求
* @param Layer 图层索引（0/1）
* @return 1=有pending swap（正在等待VBlank翻页），0=无
*
* 用途：上层可在绘制前检查，避免在swap过程中修改back buffer导致错位
*/
uint8_t LCD_IsPendingSwap(uint8_t Layer);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
