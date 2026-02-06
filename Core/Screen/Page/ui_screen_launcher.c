/**
 * @file    ui_screen_launcher_fixed.c
 * @brief   修复"方格错位"问题的Launcher（i=4或12→1时）
 *
 * 修复要点：
 * 1. 等待上一帧swap完成再开始绘制
 * 2. 添加调试信息辅助诊断
 * 3. 超时保护避免死锁
 */

#include "ui_screen_launcher.h"
#include "../../Driver/LCD/lcd.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 *                           配置常量
 * ============================================================================ */

#define N_APPS 12
#define SPACING   220.0f

#define BASE_X    60.0f
#define BASE_Y    120
#define BASE_W    200
#define BASE_H    200
#define BASE_TH   1

#define OUT_X     57.0f
#define OUT_Y     117
#define OUT_W     206
#define OUT_H     206
#define OUT_TH    3

#define COL_SEL   0xFF3DCCA3u
#define COL_NOR   0xFFF9F9F9u

#define LEFT_PAD  (OUT_X)
#define RIGHT_PAD ((float)LCD_W - OUT_X - (float)OUT_W)

#define CLEAR_Y   (OUT_Y - 4)
#define CLEAR_H   (OUT_H + 8)

#define ANIM_STIFFNESS  58.0f
#define ANIM_THRESHOLD  0.5f

// ✅ 新增：调试开关
#define DEBUG_MISALIGNMENT  0  // 设为1启用调试信息

/* ============================================================================
 *                           辅助函数
 * ============================================================================ */

static inline float f_abs(float x) {
    return (x < 0.0f) ? -x : x;
}

/* ============================================================================
 *                           初始化
 * ============================================================================ */

void Launcher_Init(void)
{
    LCD_Clear(0);
    LCD_Fill(0, LCD_COLOR_WHEAT);
    LCD_Refresh(0);

    LCD_Clear(1);
    LCD_Refresh(1);

    // LCD_DRAW



    LCD_DrawHLine(1,20,420,759,LCD_COLOR_BLACK); // 底部分割线
    LCD_DrawString(1, 650, 35, "12:34", LCD_COLOR_BLACK, 0x00000000);

#if DEBUG_MISALIGNMENT
    printf("[Launcher] Initialized\n");
#endif
}

/* ============================================================================
 *                           主循环（修复版）
 * ============================================================================ */

void Launcher_Loop(int *pi)
{
    static float scroll = 0.0f;
    static float target = 0.0f;
    static bool  animating = false;
    static int   last_i = -1;
    static uint32_t last_vblank = 0;

    // ========================================================================
    // 步骤1: VBlank节流
    // ========================================================================
    uint32_t curr_vblank = LCD_GetVBlankCount();
    if (curr_vblank == last_vblank) {
        return;  // 同一帧内，不重复绘制
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

    // ========================================================================
    // 步骤2: 【关键修复】等待上一帧swap完成
    // ========================================================================
    // 问题：如果在pending_swap=1期间继续绘制back buffer，
    //       VBlank回调swap时会显示"绘制到一半"的中间状态，导致错位
    //
    // 解决：等待swap完成（pending_swap变为0）再开始绘制

    uint32_t wait_start_vblank = curr_vblank;
    uint32_t wait_timeout = 5;  // 最多等5个VBlank（约83ms @ 60Hz）

    while (LCD_IsPendingSwap(1) && wait_timeout > 0) {
        // 等待VBlank推进
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
        // 超时仍未swap，可能LineEvent未触发或系统异常
#if DEBUG_MISALIGNMENT
        printf("[Launcher] ERROR: Swap timeout! Skip frame.\n");
#endif
        return;  // 跳过本帧，避免错位
    }

    uint32_t wait_duration = curr_vblank - wait_start_vblank;
    if (wait_duration > 0) {
#if DEBUG_MISALIGNMENT
        printf("[Launcher] Waited %lu VBlanks for swap\n", wait_duration);
#endif
    }

    // ========================================================================
    // 步骤3: 输入处理
    // ========================================================================
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

    // ========================================================================
    // 步骤4: 动画更新
    // ========================================================================
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

    // 如果没有变化且不在动画中，跳过绘制
    if (!i_changed && !animating) {
        return;
    }

    // ========================================================================
    // 步骤5: 绘制（现在可以安全绘制，因为swap已完成）
    // ========================================================================

#if DEBUG_MISALIGNMENT
    printf("[Launcher] Draw frame: i=%d, scroll=%.2f, animating=%d\n",
           i, scroll, animating);
#endif

    // 清除带状区域
    LCD_DrawRectFilledI32(1, 0, CLEAR_Y, LCD_W, CLEAR_H, 0x00000000u);

    // 绘制所有可见图标
    for (int idx = 0; idx < N_APPS; idx++) {
        // 内框
        float xb_f = BASE_X + SPACING * (float)idx - scroll;
        int xb = (int)(xb_f + 0.5f);

        if (xb >= (int)LCD_W) break;
        if ((xb + BASE_W) < 0) continue;

        LCD_DrawRectOutlineI32(1, xb, BASE_Y, BASE_W, BASE_H, BASE_TH, LCD_COLOR_BLACK);

        // 外框
        float xo_f = OUT_X + SPACING * (float)idx - scroll;
        int xo = (int)(xo_f + 0.5f);

        if (xo >= (int)LCD_W) continue;
        if ((xo + OUT_W) < 0) continue;

        uint32_t col = (idx == i) ? COL_SEL : COL_NOR;
        LCD_DrawRectOutlineI32(1, xo, OUT_Y, OUT_W, OUT_H, OUT_TH, col);
    }

    // 提交刷新
    LCD_Refresh(1);

#if DEBUG_MISALIGNMENT
    printf("[Launcher] Refresh submitted (pending_swap=1)\n");
#endif
}

/* ============================================================================
 *                           调试辅助函数
 * ============================================================================ */

/**
 * @brief 获取当前状态信息（用于调试）
 */
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
    //
    // printf("=== Launcher Status ===\n");
    // printf("VBlank rate: %.1f Hz\n", fps);
    // printf("Pending swap: %d\n", LCD_IsPendingSwap(1));
    // printf("======================\n");

    last_vblank = curr_vblank;
    last_tick = curr_tick;
}

/* ============================================================================
 *                           使用说明
 * ============================================================================ */

/*
调试步骤：

1. 设置 DEBUG_MISALIGNMENT = 1
2. 编译并运行
3. 测试 i=4 和 12→1 的切换
4. 观察串口输出：
   - "Waiting for swap": 说明在等待（正常）
   - "Swap timeout": 说明LineEvent未触发（异常）
   - "Frame skip": 说明主循环太慢或VBlank不稳定

5. 如果仍有错位：
   - 检查 LCD_DoubleBufferInit() 是否正确调用
   - 检查 LTDC 中断是否开启
   - 检查 LineEvent 计算是否正确（应为 AccumulatedActiveH + 1）

6. 验证通过后，设置 DEBUG_MISALIGNMENT = 0 关闭调试信息
*/