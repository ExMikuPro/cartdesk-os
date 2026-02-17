// ui_screen_launcher.h
// 设计稿风格启动器头文件
#ifndef UI_SCREEN_LAUNCHER_H
#define UI_SCREEN_LAUNCHER_H

#include "lvgl.h"

extern const lv_font_t lv_menu_font;

/**
 * @brief 创建设计稿风格的启动器界面
 * @param disp LVGL 显示对象，为 NULL 时使用默认显示
 */
void DesignLauncher_Create(lv_display_t *disp);

/**
 * @brief 设置选中的应用
 * @param app_index 应用索引，范围：0 到 DESIGN_APP_COUNT-1
 */
void DesignLauncher_SetSelected(int app_index);

/**
 * @brief 获取当前选中的应用
 * @return 当前选中的应用索引
 */
int DesignLauncher_GetSelected(void);

/**
 * @brief 销毁启动器界面
 */
void DesignLauncher_Destroy(void);

#endif // UI_SCREEN_LAUNCHER_H
