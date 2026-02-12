// ui_screen_launcher_lvgl.h
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* 可选：如果你是“Layer0 背景 + Layer1(LVGL)透明叠加”模式
     * 你可以在编译选项里 -DLAUNCHER_BG_TRANSPARENT=1
     * 或在某个公共配置头里 #define LAUNCHER_BG_TRANSPARENT 1
     */
#ifndef LAUNCHER_BG_TRANSPARENT
#define LAUNCHER_BG_TRANSPARENT 0
#endif

    /* 创建 Launcher UI（会创建并加载一个 screen）
     * disp 传 NULL 会使用 lv_display_get_default()
     */
    void LauncherLVGL_Create(lv_display_t *disp);

    /* 外部输入变化时调用：idx = 0..11 */
    void LauncherLVGL_SetIndex(int idx);

    /* 读取当前选择 */
    int  LauncherLVGL_GetIndex(void);

    /* （可选）兼容你原来的接口命名：不想改旧代码就用这些 */
    static inline void Launcher_Init_LVGL(void)
    {
        LauncherLVGL_Create(NULL);
    }

    static inline void Launcher_SetIndex_LVGL(int idx)
    {
        LauncherLVGL_SetIndex(idx);
    }

#ifdef __cplusplus
}
#endif
