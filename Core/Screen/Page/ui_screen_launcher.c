/**
 * @file    ui_screen_launcher_no_outline.c
 * @brief   无描边Switch样式Launcher - 优化文字渲染
 *
 * 优化要点：
 * 1. 确保文字使用完全不透明的前景色（Alpha=0xFF）
 * 2. 清除文字区域背景避免混合产生暗边
 * 3. 可选：增加文字亮度/对比度
 */

#include "jbm_el_20px.h"
#include "../../Driver/LCD/lcd.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 *                           配置常量
 * ============================================================================ */

#define N_APPS 12
#define SPACING   220.0f

#define BASE_X    60.0f
#define BASE_Y    105
#define BASE_W    200
#define BASE_H    200
#define BASE_TH   1

#define OUT_X     57.0f
#define OUT_Y     102
#define OUT_W     206
#define OUT_H     206
#define OUT_TH    3

// 文字配置（优化渲染）
#define TEXT_Y_OFFSET   (OUT_Y - 35)        // 文字位置：图标上方35像素
#define TEXT_COLOR      0xFF3DCCA3u         // 文字颜色（青绿色，Alpha必须=0xFF）
#define TEXT_BG_COLOR   0x00000000u         // 透明背景

// 可选：如果觉得颜色太暗，可以尝试更亮的青色
// #define TEXT_COLOR   0xFF4DFFB8u         // 更亮的青绿色
// #define TEXT_COLOR   0xFF5FFFD0u         // 非常亮的青色
// #define TEXT_COLOR   0xFF00FFFF u         // 纯青色（最亮）

#define COL_SEL   0xFF3DCCA3u               // 选中框颜色
#define COL_NOR   0xFFF9F9F9u               // 未选中框颜色

#define LEFT_PAD  (OUT_X)
#define RIGHT_PAD ((float)LCD_W - OUT_X - (float)OUT_W)

#define CLEAR_Y   (TEXT_Y_OFFSET - 5)
#define CLEAR_H   (OUT_H + OUT_Y - TEXT_Y_OFFSET + 10)

#define ANIM_STIFFNESS  58.0f
#define ANIM_THRESHOLD  0.5f

// 调试开关
#define DEBUG_MISALIGNMENT  0

/* ============================================================================
 *                           应用名称数据
 * ============================================================================ */

static const char* APP_NAMES[N_APPS] = {
    "Settings",     // 0:  设置
    "Gallery",      // 1:  相册
    "Music",        // 2:  音乐
    "Video",        // 3:  视频
    "Browser",      // 4:  浏览器
    "Files",        // 5:  文件管理
    "Calendar",     // 6:  日历
    "Clock",        // 7:  时钟
    "Weather",      // 8:  天气
    "Calculator",   // 9:  计算器
    "Notes",        // 10: 笔记
    "Camera"        // 11: 相机
};

/* ============================================================================
 *                           辅助函数
 * ============================================================================ */

static inline float f_abs(float x) {
    return (x < 0.0f) ? -x : x;
}

/**
 * @brief 计算字符串长度（只统计可打印ASCII字符）
 */
static int GetStringLength(const char *str)
{
    if (!str) return 0;

    int count = 0;
    const char *p = str;
    while (*p) {
        uint8_t c = (uint8_t)*p;
        if (c >= 0x20 && c <= 0x7E) {
            count++;
        }
        p++;
    }
    return count;
}

/**
 * @brief 计算字符串居中显示的X坐标
 */
static int CalcCenteredTextX(const char *str, int center_x)
{
    int char_count = GetStringLength(str);
    int text_width = char_count * FONT_ADVANCE_X;
    return center_x - (text_width / 2);
}

/**
 * @brief 绘制HUD
 */
static void Launcher_DrawHUD(uint8_t layer)
{
    LCD_DrawHLine(layer, 20, 440, 759, LCD_COLOR_BLACK);
    LCD_DrawString(layer, 600, 30, "12:34", LCD_COLOR_BLACK, 0);
    LCD_DrawString(layer, 675, 30, "100%",  LCD_COLOR_BLACK, 0);
    LCD_DrawRectFilled(layer, 735, 39, 20,  9, LCD_COLOR_BLACK);
    LCD_DrawRectOutline(layer,730, 34, 30, 19, 3, LCD_COLOR_BLACK);
    LCD_DrawRectFilled(layer, 760, 39,  3,  9, LCD_COLOR_BLACK);

    /* 五个横向排列的圆形按钮（背景 + 选择框）：按你给的写法 */
    {
        const int y    = 375;   // 你原来这颗圆的 y
        const int step = 80;    // 横向间距（可调：80~100）
        const int x0   = 400 - 2 * step; // 让 5 个以 x=400 居中

        for (int k = 0; k < 5; k++) {
            const int x = x0 + k * step;
            const uint32_t ring = (k == 2) ? COL_SEL : COL_NOR; // 默认中间选中，其它普通

            LCD_DrawCircleFilled(layer, x, y, 25, LCD_COLOR_WHEAT); // 背景=方块背景
            LCD_DrawCircle(layer,       x, y, 28, 2, ring);         // 框色=选中框色/普通框
        }
    }

}

/* ============================================================================
 *                           初始化
 * ============================================================================ */

static void WaitLayerSwapDone(uint8_t layer, uint32_t maxVBlank)
{
    uint32_t start = LCD_GetVBlankCount();
    while (LCD_IsPendingSwap(layer)) {
        if ((LCD_GetVBlankCount() - start) > maxVBlank) break;
    }
}

void Launcher_Init(void)
{
    LCD_Clear(0);
    LCD_Fill(0, LCD_COLOR_WHEAT);
    Launcher_DrawHUD(0);
    LCD_Refresh(0);

    LCD_Clear(1);
    LCD_Refresh(1);
    WaitLayerSwapDone(1, 3);
    LCD_Clear(1);
}

/* ============================================================================
 *                           主循环
 * ============================================================================ */

void Launcher_Loop(int *pi)
{
    static float scroll = 0.0f;
    static float target = 0.0f;
    static bool  animating = false;
    static int   last_i = -1;
    static uint32_t last_vblank = 0;

    /* ====================================================================
     * VBlank节流
     * ==================================================================== */
    uint32_t curr_vblank = LCD_GetVBlankCount();
    if (curr_vblank == last_vblank) {
        return;
    }

    uint32_t vblank_delta = curr_vblank - last_vblank;
    last_vblank = curr_vblank;

#if DEBUG_MISALIGNMENT
    static uint32_t frame_count = 0;
    frame_count++;
    if (vblank_delta > 1) {
        printf("[Launcher] Frame skip: vblank_delta=%lu (frame %lu)\n",
               vblank_delta, frame_count);
    }
#endif

    if (vblank_delta > 5) vblank_delta = 5;
    float dt = (float)vblank_delta * (1.0f / 60.0f);

    /* ====================================================================
     * 等待swap完成
     * ==================================================================== */
    uint32_t wait_start_vblank = curr_vblank;
    uint32_t wait_timeout = 5;

    while (LCD_IsPendingSwap(1) && wait_timeout > 0) {
        uint32_t new_vblank = LCD_GetVBlankCount();
        if (new_vblank != curr_vblank) {
            curr_vblank = new_vblank;
            wait_timeout--;

#if DEBUG_MISALIGNMENT
            printf("[Launcher] Waiting for swap... (timeout=%lu)\n", wait_timeout);
#endif
        }
    }

    if (LCD_IsPendingSwap(1)) {
#if DEBUG_MISALIGNMENT
        printf("[Launcher] ERROR: Swap timeout! Skip frame.\n");
#endif
        return;
    }

    uint32_t wait_duration = curr_vblank - wait_start_vblank;
    if (wait_duration > 0) {
#if DEBUG_MISALIGNMENT
        printf("[Launcher] Waited %lu VBlanks for swap\n", wait_duration);
#endif
    }

    /* ====================================================================
     * 输入处理
     * ==================================================================== */
    int i = *pi;
    i = (i % N_APPS + N_APPS) % N_APPS;
    *pi = i;

    bool i_changed = (i != last_i);

    if (i_changed) {
#if DEBUG_MISALIGNMENT
        printf("[Launcher] Selection changed: %d -> %d\n", last_i, i);
#endif
        last_i = i;

        float x_sel = OUT_X + SPACING * (float)i - scroll;

        target = scroll;
        if (x_sel > RIGHT_PAD) {
            target = (OUT_X + SPACING * (float)i) - RIGHT_PAD;
        } else if (x_sel < LEFT_PAD) {
            target = (OUT_X + SPACING * (float)i) - LEFT_PAD;
        }

        float max_scroll = (OUT_X + SPACING * (float)(N_APPS - 1)) - RIGHT_PAD;
        if (target < 0.0f) target = 0.0f;
        if (target > max_scroll) target = max_scroll;

#if DEBUG_MISALIGNMENT
        printf("[Launcher] Scroll: %.2f -> %.2f (delta=%.2f)\n",
               scroll, target, target - scroll);
#endif

        animating = true;
    }

    /* ====================================================================
     * 动画更新
     * ==================================================================== */
    if (animating) {
        float dx = target - scroll;
        float a = ANIM_STIFFNESS * dt;
        if (a > 1.0f) a = 1.0f;

        scroll += dx * a;

        if (f_abs(target - scroll) < ANIM_THRESHOLD) {
            scroll = target;
            animating = false;
#if DEBUG_MISALIGNMENT
            printf("[Launcher] Animation completed at scroll=%.2f\n", scroll);
#endif
        }
    }

    if (!i_changed && !animating) {
        return;
    }

    /* ====================================================================
     * 绘制
     * ==================================================================== */

#if DEBUG_MISALIGNMENT
    printf("[Launcher] Draw frame: i=%d, scroll=%.2f, animating=%d\n",
           i, scroll, animating);
#endif

    // 清除整个绘制区域
    // LCD_DrawRectFilledI32(1, 0, CLEAR_Y, LCD_W, CLEAR_H, 0x00000000u);
    LCD_DrawRectFilledI32(1, 0, CLEAR_Y, LCD_W, CLEAR_H, LCD_COLOR_WHEAT);

    // 遍历所有应用
    for (int idx = 0; idx < N_APPS; idx++) {
        // 内框位置
        float xb_f = BASE_X + SPACING * (float)idx - scroll;
        int xb = (int)(xb_f + 0.5f);

        if (xb >= (int)LCD_W) break;
        if ((xb + BASE_W) < 0) continue;

        // 绘制内框
        LCD_DrawRectOutlineI32(1, xb, BASE_Y, BASE_W, BASE_H, BASE_TH, LCD_COLOR_BLACK);

        // 外框位置
        float xo_f = OUT_X + SPACING * (float)idx - scroll;
        int xo = (int)(xo_f + 0.5f);

        if (xo >= (int)LCD_W) continue;
        if ((xo + OUT_W) < 0) continue;

        bool is_selected = (idx == i);

        // 绘制外框
        uint32_t frame_color = is_selected ? COL_SEL : COL_NOR;
        LCD_DrawRectOutlineI32(1, xo, OUT_Y, OUT_W, OUT_H, OUT_TH, frame_color);

        /* ====================================================================
         * 【优化】仅选中时绘制文字，确保颜色完全不透明
         * ==================================================================== */
        if (is_selected) {
            const char *app_name = APP_NAMES[idx];

            // 计算文字位置
            int icon_center_x = xb + BASE_W / 2;
            int text_x = CalcCenteredTextX(app_name, icon_center_x);

            // 【重要】确保文字颜色的Alpha通道=0xFF（完全不透明）
            // 这样可以避免与背景混合产生暗边
            uint32_t text_color = TEXT_COLOR | 0xFF000000u;

            // 绘制文字（透明背景）
            LCD_DrawString(1, text_x, TEXT_Y_OFFSET, app_name,
                          text_color, 0);
        }
    }

    LCD_Refresh(1);

#if DEBUG_MISALIGNMENT
    printf("[Launcher] Refresh submitted (pending_swap=1)\n");
#endif
}

/* ============================================================================
 *                           调试函数
 * ============================================================================ */

void Launcher_PrintStatus(void)
{
    static uint32_t last_vblank = 0;
    static uint32_t last_tick = 0;

    uint32_t curr_vblank = LCD_GetVBlankCount();
    uint32_t curr_tick = HAL_GetTick();

    if (last_tick == 0) {
        last_vblank = curr_vblank;
        last_tick = curr_tick;
        return;
    }

    uint32_t dt_ms = curr_tick - last_tick;
    if (dt_ms < 1000) return;

    uint32_t vblank_count = curr_vblank - last_vblank;
    float fps = (float)vblank_count * 1000.0f / (float)dt_ms;

    last_vblank = curr_vblank;
    last_tick = curr_tick;
}

/* ============================================================================
 *                           解决"黑色描边"问题的方法
 * ============================================================================ */

/*
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                    🔍 "黑色描边"问题分析
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

问题原因：
1. 字体抗锯齿：JetBrains Mono使用A8 alpha mask，边缘像素是半透明的
2. Alpha混合：半透明像素与背景混合时，可能产生深色边缘
3. 背景影响：如果背景不是纯色，会影响文字边缘的颜色

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                    ✅ 解决方案（按优先级）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

方案1：【已实现】确保颜色完全不透明
------------------------------------------------
代码第317行：
    uint32_t text_color = TEXT_COLOR | 0xFF000000u;

强制设置Alpha=0xFF，避免透明度混合问题


方案2：使用更亮的文字颜色
------------------------------------------------
在第35-40行选择更亮的颜色：

// 当前颜色（可能显得较暗）
#define TEXT_COLOR 0xFF3DCCA3u

// 更亮的选项：
#define TEXT_COLOR 0xFF4DFFB8u    // 亮青绿色
#define TEXT_COLOR 0xFF5FFFD0u    // 很亮的青色
#define TEXT_COLOR 0xFF00FFFFu    // 纯青色（最亮）
#define TEXT_COLOR 0xFFFFFFFFu    // 纯白色（最清晰）


方案3：修改字体生成参数（需要重新生成字库）
------------------------------------------------
如果使用的是自己生成的字体：

1. 减少抗锯齿强度
2. 使用更粗的字体（如Regular而不是Thin）
3. 增加字体大小
4. 调整字体渲染的gamma值

示例（如果你有字体生成工具）：
- 当前：JetBrains Mono THIN 20px
- 建议：JetBrains Mono REGULAR 22px（更清晰）


方案4：添加文字背景（类似标签效果）
------------------------------------------------
修改第36行：

// 从透明背景
#define TEXT_BG_COLOR 0x00000000u

// 改为半透明深色背景
#define TEXT_BG_COLOR 0x80000000u  // 半透明黑色
// 或纯色背景
#define TEXT_BG_COLOR 0xFF000000u  // 纯黑色背景

这会在文字下方添加一个背景框，类似很多UI的标签效果


方案5：检查背景层（Layer0）
------------------------------------------------
确保背景层是纯色且完全不透明：

void Launcher_Init(void) {
    LCD_Clear(0);
    LCD_Fill(0, LCD_COLOR_WHEAT);  // 确保这是纯色
    // ...
}

如果背景不是纯色或有渐变，会影响文字显示


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                    🎨 推荐配置（清晰无描边）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

配置1：纯青色文字（最亮最清晰）
#define TEXT_COLOR 0xFF00FFFFu

配置2：亮绿青色（Switch风格但更亮）
#define TEXT_COLOR 0xFF5FFFD0u

配置3：纯白色（最高对比度）
#define TEXT_COLOR 0xFFFFFFFFu

配置4：带黑色背景标签（最清晰）
#define TEXT_COLOR    0xFFFFFFFFu  // 白色文字
#define TEXT_BG_COLOR 0xCC000000u  // 半透明黑色背景

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                    🔧 如何测试
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. 首先尝试方案1（已在代码中实现）
2. 如果仍有描边，尝试方案2（修改TEXT_COLOR为更亮的颜色）
3. 如果还是不满意，考虑方案4（添加背景框）

快速测试不同颜色：
直接修改第35行的 TEXT_COLOR 定义，重新编译即可

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
*/