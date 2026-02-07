// ui_screen_launcher_lvgl.c
#include "ui_screen_launcher.h"
#include <stdbool.h>
#include <stdint.h>

#include "jbm_el_20px.h"   // 你的字体：确保这里导出的是 lv_font_t

/* ===================== 你原来的宏参数 ===================== */
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

#define TEXT_Y_OFFSET   (OUT_Y - 35)

/* 颜色：用 lv_color_hex + opa 来表示
 * 你原来 0xFF3DCCA3u -> RGB = 3DCCA3, A=FF
 */
#define COL_SEL_RGB  0x3DCCA3
#define COL_NOR_RGB  0xF9F9F9
#define COL_BLK_RGB  0x000000
#define COL_WHEAT_RGB 0xF5DEB3   // 如果你 Layer0 已经画背景，这个可不用

/* 如果你使用“Layer0 背景 + Layer1(LVGL)透明叠加”，把这个设为 1 */
#ifndef LAUNCHER_BG_TRANSPARENT
#define LAUNCHER_BG_TRANSPARENT 0
#endif

/* ===================== 应用名 ===================== */
static const char* APP_NAMES[N_APPS] = {
    "Settings","Gallery","Music","Video","Browser","Files",
    "Calendar","Clock","Weather","Calculator","Notes","Camera"
};

/* ===================== 内部状态 ===================== */
static lv_display_t *s_disp = NULL;
static lv_obj_t *s_scr = NULL;

static lv_obj_t *s_hud_line = NULL;
static lv_obj_t *s_time = NULL;
static lv_obj_t *s_batt = NULL;
static lv_obj_t *s_batt_cap = NULL;
static lv_obj_t *s_batt_fill = NULL;

static lv_obj_t *s_nav_btn[5] = {0};

typedef struct {
    lv_obj_t *root;   // 外框（OUT_W x OUT_H）
    lv_obj_t *inner;  // 内框（BASE_W x BASE_H）
    lv_obj_t *label;  // 选中文字
} app_item_t;

static app_item_t s_app[N_APPS];

static int s_sel = 0;

/* scroll 用固定点：/256 */
static int32_t s_scroll_fp = 0;
static int32_t s_target_fp = 0;

static int32_t fp_from_f(float v) { return (int32_t)(v * 256.0f + (v >= 0 ? 0.5f : -0.5f)); }
static float   f_from_fp(int32_t v) { return (float)v / 256.0f; }

/* ===================== 样式 ===================== */
static lv_style_t st_outer;
static lv_style_t st_inner;
static lv_style_t st_label;
static lv_style_t st_hud_text;
static lv_style_t st_hud_line;
static lv_style_t st_nav_btn;

static void styles_init(void)
{
    lv_style_init(&st_outer);
    lv_style_set_bg_opa(&st_outer, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_outer, OUT_TH);
    lv_style_set_border_color(&st_outer, lv_color_hex(COL_NOR_RGB));
    lv_style_set_radius(&st_outer, 12);
    lv_style_set_pad_all(&st_outer, 0);
    lv_style_set_outline_width(&st_outer, 0);
    lv_style_set_shadow_width(&st_outer, 0);

    lv_style_init(&st_inner);
    lv_style_set_bg_opa(&st_inner, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_inner, BASE_TH);
    lv_style_set_border_color(&st_inner, lv_color_hex(COL_BLK_RGB));
    lv_style_set_radius(&st_inner, 10);
    lv_style_set_pad_all(&st_inner, 0);

    lv_style_init(&st_label);
    lv_style_set_text_font(&st_label, &lv_font_montserrat_20);
    lv_style_set_text_color(&st_label, lv_color_hex(COL_SEL_RGB));
    lv_style_set_text_opa(&st_label, LV_OPA_COVER);     // 强制文字不透明（你原来的“Alpha=FF”诉求）
    lv_style_set_bg_opa(&st_label, LV_OPA_TRANSP);

    lv_style_init(&st_hud_text);
    lv_style_set_text_color(&st_hud_text, lv_color_hex(COL_BLK_RGB));
    lv_style_set_text_opa(&st_hud_text, LV_OPA_COVER);

    lv_style_init(&st_hud_line);
    lv_style_set_bg_color(&st_hud_line, lv_color_hex(COL_BLK_RGB));
    lv_style_set_bg_opa(&st_hud_line, LV_OPA_COVER);

    lv_style_init(&st_nav_btn);
    lv_style_set_radius(&st_nav_btn, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&st_nav_btn, LV_OPA_COVER);
#if LAUNCHER_BG_TRANSPARENT
    lv_style_set_bg_color(&st_nav_btn, lv_color_hex(0x000000)); // 透明叠加时，这里你可以改成透明或不画
    lv_style_set_bg_opa(&st_nav_btn, LV_OPA_TRANSP);
#else
    lv_style_set_bg_color(&st_nav_btn, lv_color_hex(COL_WHEAT_RGB));
#endif
    lv_style_set_border_width(&st_nav_btn, 2);
}

/* ===================== 位置/可见性更新 ===================== */
static void update_selected_style(void)
{
    for(int k = 0; k < N_APPS; k++) {
        lv_color_t c = (k == s_sel) ? lv_color_hex(COL_SEL_RGB) : lv_color_hex(COL_NOR_RGB);
        lv_obj_set_style_border_color(s_app[k].root, c, 0);

        if(k == s_sel) {
            lv_label_set_text(s_app[k].label, APP_NAMES[k]);
            lv_obj_clear_flag(s_app[k].label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_app[k].label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void update_positions(void)
{
    const int32_t disp_w = (int32_t)lv_display_get_horizontal_resolution(s_disp);

    const float scroll = f_from_fp(s_scroll_fp);

    for(int idx = 0; idx < N_APPS; idx++) {
        float xo_f = OUT_X + SPACING * (float)idx - scroll;
        int32_t xo = (int32_t)(xo_f + 0.5f);

        /* 超出屏幕就隐藏，减少绘制负担（可选） */
        if(xo > disp_w || (xo + OUT_W) < 0) {
            lv_obj_add_flag(s_app[idx].root, LV_OBJ_FLAG_HIDDEN);
            continue;
        } else {
            lv_obj_clear_flag(s_app[idx].root, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_set_pos(s_app[idx].root, xo, OUT_Y);

        /* 文字：只在选中时显示，位置固定在图标上方 35px，并居中 */
        if(idx == s_sel) {
            lv_obj_update_layout(s_app[idx].label);
            lv_obj_set_pos(
                s_app[idx].label,
                (OUT_W - (int32_t)lv_obj_get_width(s_app[idx].label)) / 2,
                (TEXT_Y_OFFSET - OUT_Y)
            );
        }
    }
}

static void scroll_anim_exec(void *var, int32_t v)
{
    (void)var;
    s_scroll_fp = v;
    update_positions();
}

/* 计算 target scroll：把选中的框保持在 LEFT/RIGHT PAD 内 */
static void compute_target_for_index(int i)
{
    const int32_t disp_w = (int32_t)lv_display_get_horizontal_resolution(s_disp);

    const float right_pad = (float)disp_w - OUT_X - OUT_W;
    const float left_pad  = OUT_X;

    float scroll = f_from_fp(s_scroll_fp);

    float x_sel = OUT_X + SPACING * (float)i - scroll;

    float target = scroll;
    if(x_sel > right_pad) {
        target = (OUT_X + SPACING * (float)i) - right_pad;
    } else if(x_sel < left_pad) {
        target = (OUT_X + SPACING * (float)i) - left_pad;
    }

    float max_scroll = (OUT_X + SPACING * (float)(N_APPS - 1)) - right_pad;
    if(target < 0.0f) target = 0.0f;
    if(target > max_scroll) target = max_scroll;

    s_target_fp = fp_from_f(target);
}

static void start_scroll_anim(void)
{
    lv_anim_del(&s_scroll_fp, scroll_anim_exec);

    int32_t from = s_scroll_fp;
    int32_t to   = s_target_fp;

    int32_t dist = (to > from) ? (to - from) : (from - to);
    uint32_t px = (uint32_t)(dist / 256);

    /* 动画时间：按距离自适应（你原来 stiffness 的感觉） */
    uint32_t time = 90 + px * 2;
    if(time < 120) time = 120;
    if(time > 260) time = 260;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_scroll_fp);
    lv_anim_set_exec_cb(&a, scroll_anim_exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ===================== HUD 创建 ===================== */
static void create_hud(lv_obj_t *parent)
{
    /* 你原来的横线：x=20, y=440, len=759 */
    s_hud_line = lv_obj_create(parent);
    lv_obj_remove_style_all(s_hud_line);
    lv_obj_add_style(s_hud_line, &st_hud_line, 0);
    lv_obj_set_pos(s_hud_line, 20, 440);
    lv_obj_set_size(s_hud_line, 759, 1);
    lv_obj_clear_flag(s_hud_line, LV_OBJ_FLAG_SCROLLABLE);

    s_time = lv_label_create(parent);
    lv_obj_add_style(s_time, &st_hud_text, 0);
    lv_obj_set_pos(s_time, 600, 30);
    lv_label_set_text(s_time, "12:34");

    lv_obj_t *pct = lv_label_create(parent);
    lv_obj_add_style(pct, &st_hud_text, 0);
    lv_obj_set_pos(pct, 675, 30);
    lv_label_set_text(pct, "100%");

    /* 电池：用 3 个小矩形模拟 */
    s_batt = lv_obj_create(parent);
    lv_obj_remove_style_all(s_batt);
    lv_obj_set_pos(s_batt, 730, 34);
    lv_obj_set_size(s_batt, 30, 19);
    lv_obj_set_style_border_width(s_batt, 3, 0);
    lv_obj_set_style_border_color(s_batt, lv_color_hex(COL_BLK_RGB), 0);
    lv_obj_set_style_bg_opa(s_batt, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_batt, LV_OBJ_FLAG_SCROLLABLE);

    s_batt_fill = lv_obj_create(parent);
    lv_obj_remove_style_all(s_batt_fill);
    lv_obj_set_pos(s_batt_fill, 735, 39);
    lv_obj_set_size(s_batt_fill, 20, 9);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(COL_BLK_RGB), 0);
    lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_batt_fill, LV_OBJ_FLAG_SCROLLABLE);

    s_batt_cap = lv_obj_create(parent);
    lv_obj_remove_style_all(s_batt_cap);
    lv_obj_set_pos(s_batt_cap, 760, 39);
    lv_obj_set_size(s_batt_cap, 3, 9);
    lv_obj_set_style_bg_color(s_batt_cap, lv_color_hex(COL_BLK_RGB), 0);
    lv_obj_set_style_bg_opa(s_batt_cap, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_batt_cap, LV_OBJ_FLAG_SCROLLABLE);

    /* 五个圆形按钮：y=375，step=80，x0=400-2*step */
    const int y = 375;
    const int step = 80;
    const int x0 = 400 - 2 * step;

    for(int k = 0; k < 5; k++) {
        s_nav_btn[k] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_nav_btn[k]);
        lv_obj_add_style(s_nav_btn[k], &st_nav_btn, 0);

        lv_obj_set_size(s_nav_btn[k], 56, 56);
        lv_obj_set_pos(s_nav_btn[k], x0 + k * step - 28, y - 28); // 以中心定位
        lv_obj_clear_flag(s_nav_btn[k], LV_OBJ_FLAG_SCROLLABLE);

        lv_color_t ring = (k == 2) ? lv_color_hex(COL_SEL_RGB) : lv_color_hex(COL_NOR_RGB);
        lv_obj_set_style_border_color(s_nav_btn[k], ring, 0);
    }
}

/* ===================== Apps 创建 ===================== */
static void create_apps(lv_obj_t *parent)
{
    const int inner_off_x = (int)(BASE_X - OUT_X); // 3
    const int inner_off_y = (int)(BASE_Y - OUT_Y); // 3

    for(int i = 0; i < N_APPS; i++) {
        s_app[i].root = lv_obj_create(parent);
        lv_obj_remove_style_all(s_app[i].root);
        lv_obj_add_style(s_app[i].root, &st_outer, 0);
        lv_obj_set_size(s_app[i].root, OUT_W, OUT_H);
        lv_obj_clear_flag(s_app[i].root, LV_OBJ_FLAG_SCROLLABLE);

        s_app[i].inner = lv_obj_create(s_app[i].root);
        lv_obj_remove_style_all(s_app[i].inner);
        lv_obj_add_style(s_app[i].inner, &st_inner, 0);
        lv_obj_set_pos(s_app[i].inner, inner_off_x, inner_off_y);
        lv_obj_set_size(s_app[i].inner, BASE_W, BASE_H);
        lv_obj_clear_flag(s_app[i].inner, LV_OBJ_FLAG_SCROLLABLE);

        s_app[i].label = lv_label_create(s_app[i].root);
        lv_obj_add_style(s_app[i].label, &st_label, 0);
        lv_label_set_text(s_app[i].label, APP_NAMES[i]);
        lv_obj_add_flag(s_app[i].label, LV_OBJ_FLAG_HIDDEN); // 默认隐藏，选中才显示
    }

    update_selected_style();
    update_positions();
}

/* ===================== 对外 API ===================== */
void LauncherLVGL_Create(lv_display_t *disp)
{
    s_disp = disp ? disp : lv_display_get_default();
    if(!s_disp) return;

    styles_init();

    /* 新建 screen */
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

#if LAUNCHER_BG_TRANSPARENT
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_TRANSP, 0);
#else
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_WHEAT_RGB), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
#endif

    create_hud(s_scr);
    create_apps(s_scr);

    lv_screen_load(s_scr);

    /* 默认选中 0 */
    s_sel = 0;
    s_scroll_fp = 0;
    compute_target_for_index(s_sel);
    update_selected_style();
    update_positions();
}

void LauncherLVGL_SetIndex(int idx)
{
    if(!s_disp || !s_scr) return;
    if(idx < 0) idx = 0;
    if(idx >= N_APPS) idx = N_APPS - 1;

    if(idx == s_sel) return;

    s_sel = idx;
    update_selected_style();

    compute_target_for_index(s_sel);
    start_scroll_anim();
}

int LauncherLVGL_GetIndex(void)
{
    return s_sel;
}
