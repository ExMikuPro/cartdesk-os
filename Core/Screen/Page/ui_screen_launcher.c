// ui_screen_launcher.c
// 设计稿风格启动器实现

#include "ui_screen_launcher.h"

/* ------------------------------------------------------------------ */
/*  常量配置                                                            */
/* ------------------------------------------------------------------ */

#define DESIGN_APP_COUNT      12
#define DESIGN_CIRCLE_COUNT   5

#define BOX_WIDTH             200
#define BOX_HEIGHT            200
#define BOX_SPACING           20
#define BOX_Y_OFFSET          80   /* 方块在内容容器内的 Y 偏移 */
#define BOX_CONTAINER_Y       26   /* 方块容器相对于主容器的 Y */

#define CIRCLE_RADIUS         28
#define CIRCLE_SPACING        24
#define CIRCLE_Y              330  /* 圆形相对于主容器的 Y */

#define LINE_Y                420
#define LINE_X                40
#define LINE_WIDTH            720

#define SCREEN_W              800
#define SCREEN_H              480

#define COLOR_BG              0xFFFFFF
#define COLOR_BLACK           0x000000
#define COLOR_CYAN            0x00FFFF

/* ------------------------------------------------------------------ */
/*  私有状态                                                            */
/* ------------------------------------------------------------------ */

static const char *app_names[DESIGN_APP_COUNT] = {
    "app1",  "app2",  "app3",  "app4",
    "app5",  "app6",  "app7",  "app8",
    "app9",  "app10", "app11", "app12"
};

static const char *circle_names[DESIGN_CIRCLE_COUNT] = {
    "Gallery", "Handle", "Expansion", "Setting", "Sleep Mode"
};

static lv_obj_t *s_main_container  = NULL;
static lv_obj_t *s_boxes[DESIGN_APP_COUNT];
static lv_obj_t *s_box_labels[DESIGN_APP_COUNT];
static lv_obj_t *s_circles[DESIGN_CIRCLE_COUNT];
static lv_obj_t *s_circle_labels[DESIGN_CIRCLE_COUNT];
static int        s_selected_index  = 0;  /* 当前选中的 app 索引（-1 表示圆形选中） */

/* ------------------------------------------------------------------ */
/*  内部工具                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 重置所有元素到未选中状态，然后高亮 selected_obj
 */
static void prv_set_selection(lv_obj_t *selected_obj)
{
    /* 重置所有方块 */
    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        lv_obj_set_style_border_color(s_boxes[i], lv_color_hex(COLOR_BLACK), 0);
        lv_obj_add_flag(s_box_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 重置所有圆形 */
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        lv_obj_set_style_border_color(s_circles[i], lv_color_hex(COLOR_BLACK), 0);
        lv_obj_set_style_border_width(s_circles[i], 1, 0);
        lv_obj_add_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (selected_obj == NULL) return;

    lv_obj_set_style_border_color(selected_obj, lv_color_hex(COLOR_CYAN), 0);

    /* 检查是否为方块 */
    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        if (s_boxes[i] == selected_obj) {
            lv_obj_clear_flag(s_box_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = i;
            return;
        }
    }

    /* 检查是否为圆形 */
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        if (s_circles[i] == selected_obj) {
            lv_obj_set_style_border_width(s_circles[i], 3, 0);
            lv_obj_clear_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = -(i + 1);  /* 用负数区分圆形 */
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  事件回调                                                            */
/* ------------------------------------------------------------------ */

static void prv_box_clicked_cb(lv_event_t *e)
{
    prv_set_selection(lv_event_get_target(e));
}

static void prv_circle_clicked_cb(lv_event_t *e)
{
    prv_set_selection(lv_event_get_target(e));
}

/* ------------------------------------------------------------------ */
/*  子模块创建函数                                                       */
/* ------------------------------------------------------------------ */

static void prv_create_box_area(lv_obj_t *parent)
{
    /* 计算尺寸 */
    const int container_height = BOX_Y_OFFSET + BOX_HEIGHT + 10;
    const int content_width    = DESIGN_APP_COUNT * (BOX_WIDTH + BOX_SPACING) + 20;

    /* ---- 方块容器（可水平滚动） ---- */
    lv_obj_t *box_container = lv_obj_create(parent);
    lv_obj_set_size(box_container, SCREEN_W, container_height + 60);
    lv_obj_set_y(box_container, BOX_CONTAINER_Y);
    lv_obj_set_style_bg_color(box_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(box_container, 0, 0);
    lv_obj_set_style_pad_all(box_container, 0, 0);
    lv_obj_set_scrollbar_mode(box_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(box_container, LV_DIR_HOR);
    lv_obj_set_style_anim_duration(box_container, 0, 0);  // 单位 ms，值越小越快越不平滑，值越大越慢越平滑
    lv_obj_clear_flag(box_container, LV_OBJ_FLAG_SCROLL_ELASTIC);

    /* ---- 内容容器（承载所有方块，宽度超出父容器触发父级滚动） ---- */
    lv_obj_t *content_container = lv_obj_create(box_container);
    lv_obj_set_size(content_container, content_width, container_height + 60);
    lv_obj_set_style_bg_color(content_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 0, 0);
    lv_obj_set_scrollbar_mode(content_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 循环创建方块和对应标签 ---- */
    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        const int box_x = 20+i * (BOX_WIDTH + BOX_SPACING);

        /* 方块 */
        lv_obj_t *box = lv_obj_create(content_container);
        lv_obj_set_size(box, BOX_WIDTH, BOX_HEIGHT);
        lv_obj_set_pos(box, box_x, BOX_Y_OFFSET);
        lv_obj_set_style_bg_color(box, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_border_color(box,
            (i == 0) ? lv_color_hex(COLOR_CYAN) : lv_color_hex(COLOR_BLACK), 0);
        lv_obj_add_event_cb(box, prv_box_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_boxes[i] = box;

        /* 方块标题标签（默认隐藏，选中后显示） */
        lv_obj_t *label = lv_label_create(content_container);
        lv_label_set_text(label, app_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_pos(label, box_x,45);
        lv_obj_set_width(label, BOX_WIDTH);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        s_box_labels[i] = label;
    }

    /* 默认选中第一个方块 */
    lv_obj_clear_flag(s_box_labels[0], LV_OBJ_FLAG_HIDDEN);
}

static void prv_create_circle_area(lv_obj_t *parent)
{
    const int diameter          = CIRCLE_RADIUS * 2;
    const int total_width       = DESIGN_CIRCLE_COUNT * diameter
                                  + (DESIGN_CIRCLE_COUNT - 1) * CIRCLE_SPACING;
    const int start_x           = (SCREEN_W - total_width) / 2;

    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        const int cx = start_x + i * (diameter + CIRCLE_SPACING);

        /* 圆形 */
        lv_obj_t *circle = lv_obj_create(parent);
        lv_obj_set_size(circle, diameter, diameter);
        lv_obj_set_pos(circle, cx, CIRCLE_Y);
        lv_obj_set_style_radius(circle, CIRCLE_RADIUS, 0);
        lv_obj_set_style_bg_color(circle, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_border_width(circle, 1, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(COLOR_BLACK), 0);
        lv_obj_add_event_cb(circle, prv_circle_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_circles[i] = circle;

        /* 圆形标题标签（圆形下方，默认隐藏） */
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, circle_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(label, cx - 40, CIRCLE_Y + diameter + 5);
        lv_obj_set_width(label, diameter + 80);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        s_circle_labels[i] = label;
    }
}

static void prv_create_divider_line(lv_obj_t *parent)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, LINE_WIDTH, 2);
    lv_obj_set_pos(line, LINE_X, LINE_Y);
    lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_BLACK), 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

/* ------------------------------------------------------------------ */
/*  公开 API                                                            */
/* ------------------------------------------------------------------ */

void DesignLauncher_Create(lv_display_t *disp)
{
    lv_obj_t *scr = (disp != NULL)
                    ? lv_display_get_screen_active(disp)
                    : lv_scr_act();

    /* 清除屏幕内边距 */
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* 主容器 */
    s_main_container = lv_obj_create(scr);
    lv_obj_set_size(s_main_container, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_main_container, 0, 0);
    lv_obj_set_style_bg_color(s_main_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_main_container, 0, 0);
    lv_obj_set_style_pad_all(s_main_container, 0, 0);
    lv_obj_clear_flag(s_main_container, LV_OBJ_FLAG_SCROLLABLE);

    /* 子区域 */
    prv_create_box_area(s_main_container);
    prv_create_circle_area(s_main_container);
    prv_create_divider_line(s_main_container);

    /* 初始化选中状态 */
    s_selected_index = 0;
}

void DesignLauncher_SetSelected(int app_index)
{
    if (app_index < 0 || app_index >= DESIGN_APP_COUNT) return;
    prv_set_selection(s_boxes[app_index]);
}

int DesignLauncher_GetSelected(void)
{
    return s_selected_index;
}

void DesignLauncher_Destroy(void)
{
    if (s_main_container != NULL) {
        lv_obj_delete(s_main_container);
        s_main_container = NULL;
    }
    s_selected_index = 0;
}