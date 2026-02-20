// ui_screen_launcher.c
// 设计稿风格启动器实现
// 图片数据直接定位在 SDRAM heap 区，DMA2D 搬运，不经过 Flash

#include "ui_screen_launcher.h"

#include <string.h>

#include "stm32h743xx.h"
#include "Core/Screen/utils/cart_reader.h"


/* ------------------------------------------------------------------ */
/*  SDRAM 地址布局                                                      */
/* ------------------------------------------------------------------ */

/*
 * 帧缓冲由 ltdc.c 管理，本文件只使用 heap 区（0xD0465000 以上）。
 *
 *  0xD0000000  Layer0 FB          (0x177000 B)
 *  0xD0177000  Layer1 Front       (0x177000 B)
 *  0xD02EE000  Layer1 Back        (0xD0177000 B)
 *  0xD0465000  ← SDRAM heap 起始，图片缓冲从这里开始
 *
 * 每张图片 200×200×4 = 0x3E800 字节，预留 12 个槽。
 * 总占用: 12 × 0x3E800 = 0x2E6000 B ≈ 2.9 MB，绰绰有余。
 *
 * 注意：如果你使用了 FreeRTOS+heap_5 或 STM32Cube SDRAM heap，
 *       把起始地址 SDRAM_IMG_BASE 配置进你的 heap 区域即可；
 *       如果是静态分配，直接用下面的宏指针访问没有问题。
 */


/* ------------------------------------------------------------------ */
/*  常量配置                                                            */
/* ------------------------------------------------------------------ */

#define DESIGN_APP_COUNT      12
#define DESIGN_CIRCLE_COUNT   5

#define BOX_WIDTH             200
#define BOX_HEIGHT            200
#define BOX_SPACING           20
#define BOX_Y_OFFSET          80
#define BOX_CONTAINER_Y       26

#define CIRCLE_RADIUS         28
#define CIRCLE_SPACING        24
#define CIRCLE_Y              330

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

static char s_cart0_title[65] = "LOADING";

/*
 * Flash 源数据指针数组（只读，用于初始化时搬运到 SDRAM）。
 * NULL 表示该槽无图片。
 */
static const uint8_t *s_slot_flash_src[DESIGN_APP_COUNT] = {
    NULL,  // 预览图片现在从 SD 卡读取
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

static const char *app_names[DESIGN_APP_COUNT] = {
    "appaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "app2",  "app3",  "app4",
    "app5",  "app6",  "app7",  "app8",
    "app9",  "app10", "app11", "app12"
};

static const char *circle_names[DESIGN_CIRCLE_COUNT] = {
    "相册", "手柄", "拓展", "设置", "休眠模式"
};

static lv_obj_t *s_main_container = NULL;
static lv_obj_t *s_slots[DESIGN_APP_COUNT];
static lv_obj_t *s_slot_labels[DESIGN_APP_COUNT];
static lv_obj_t *s_circles[DESIGN_CIRCLE_COUNT];
static lv_obj_t *s_circle_labels[DESIGN_CIRCLE_COUNT];

/*
 * 每个槽独立的 LVGL 图像描述符。
 * .data 直接指向 SDRAM 地址，LTDC/DMA2D 原生支持，零拷贝渲染。
 */
static lv_image_dsc_t s_image_dsc[DESIGN_APP_COUNT];

static int s_selected_index = 0;

/* ------------------------------------------------------------------ */
/*  内部工具：DMA2D 从内存搬运到 SDRAM                                */
/* ------------------------------------------------------------------ */

/*
 * prv_copy_img_to_sdram()
 *
 * 用 DMA2D 的 M2M 模式把图片从内存（Flash 或 RAM）
 * 搬运到 SDRAM。DMA2D 支持直接读 Flash AXI 地址，CPU 完全不参与，
 * 搬运期间 CPU 可以继续跑其他初始化逻辑。
 *
 * 这里使用同步等待方式（轮询 TC 标志），如果你想异步，
 * 改为中断模式并在回调里设置信号量即可。
 */
static void prv_copy_img_to_sdram(uint32_t dst, const uint8_t *src, uint32_t size)
{
    /* 逐行搬运，每行 IMG_STRIDE 字节，共 IMG_H 行 */
    DMA2D->CR      = 0x00000000UL;          /* M2M 模式，无色彩转换 */
    DMA2D->FGMAR   = (uint32_t)src;         /* 源：内存地址 */
    DMA2D->OMAR    = dst;                    /* 目标：SDRAM 地址 */
    DMA2D->FGOR    = 0;                      /* 源行偏移 0（连续） */
    DMA2D->OOR     = 0;                      /* 目标行偏移 0 */
    DMA2D->FGPFCCR = 0x00000000UL;          /* 源格式 ARGB8888 */
    DMA2D->OPFCCR  = 0x00000000UL;          /* 目标格式 ARGB8888 */
    /* NLR: 每行像素数 | 行数 */
    DMA2D->NLR     = (uint32_t)(IMG_W) | ((uint32_t)(IMG_H) << 16);
    DMA2D->CR     |= DMA2D_CR_START;        /* 启动 */

    /* 等待完成（TC 标志） */
    while (!(DMA2D->ISR & DMA2D_ISR_TCIF)) { __NOP(); }
    DMA2D->IFCR = DMA2D_IFCR_CTCIF;        /* 清除标志 */
}

/* ------------------------------------------------------------------ */
/*  内部工具：选中状态                                                  */
/* ------------------------------------------------------------------ */

static void prv_set_selection(lv_obj_t *selected_obj)
{
    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        lv_obj_set_style_border_color(s_slots[i], lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
        lv_obj_add_flag(s_slot_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        lv_obj_set_style_border_color(s_circles[i], lv_color_hex(COLOR_BLACK), 0);
        lv_obj_set_style_border_width(s_circles[i], 1, 0);
        lv_obj_add_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (selected_obj == NULL) return;

    lv_obj_set_style_border_color(selected_obj, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        if (s_slots[i] == selected_obj) {
            lv_obj_clear_flag(s_slot_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = i;
            return;
        }
    }
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        if (s_circles[i] == selected_obj) {
            lv_obj_set_style_border_width(s_circles[i], 3, 0);
            lv_obj_clear_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = -(i + 1);
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
/*  子模块创建                                                          */
/* ------------------------------------------------------------------ */

static void prv_create_box_area(lv_obj_t *parent)
{
    const int container_height = BOX_Y_OFFSET + BOX_HEIGHT + 10;
    const int content_width    = DESIGN_APP_COUNT * (BOX_WIDTH + BOX_SPACING) + 20;

    lv_obj_t *box_container = lv_obj_create(parent);
    lv_obj_set_size(box_container, SCREEN_W, container_height + 60);
    lv_obj_set_y(box_container, BOX_CONTAINER_Y);
    lv_obj_set_style_bg_color(box_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(box_container, 0, 0);
    lv_obj_set_style_pad_all(box_container, 0, 0);
    lv_obj_set_scrollbar_mode(box_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(box_container, LV_DIR_HOR);
    lv_obj_set_style_anim_duration(box_container, 0, 0);
    lv_obj_clear_flag(box_container, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t *content_container = lv_obj_create(box_container);
    lv_obj_set_size(content_container, content_width, container_height + 60);
    lv_obj_set_style_bg_color(content_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 0, 0);
    lv_obj_set_scrollbar_mode(content_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        const int box_x = 20 + i * (BOX_WIDTH + BOX_SPACING);

        lv_obj_t *slot_container = lv_obj_create(content_container);
        lv_obj_set_size(slot_container, BOX_WIDTH, BOX_HEIGHT);
        lv_obj_set_pos(slot_container, box_x, BOX_Y_OFFSET);
        lv_obj_set_style_bg_color(slot_container, lv_color_hex(COLOR_BG), LV_PART_MAIN);
        lv_obj_set_style_border_width(slot_container, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(slot_container, 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(slot_container,
            (i == 0) ? lv_color_hex(COLOR_CYAN) : lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
        lv_obj_clear_flag(slot_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(slot_container, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(slot_container, LV_OBJ_FLAG_CLICKABLE);

        /*
         * 如果该槽有图片，且 DMA2D 已在 DesignLauncher_Create 里完成了
         * Flash→SDRAM 的搬运，这里直接用 SDRAM 地址作为图片数据源。
         * LVGL 渲染时 DMA2D 读 SDRAM，带宽充足，CPU 完全不参与。
         */
        if (s_image_dsc[i].data != NULL) {
            lv_obj_t *img_obj = lv_img_create(slot_container);
            lv_obj_set_size(img_obj, BOX_WIDTH, BOX_HEIGHT);
            lv_obj_center(img_obj);
            lv_img_set_src(img_obj, &s_image_dsc[i]);
            lv_obj_set_style_border_width(img_obj, 0, LV_PART_MAIN);
            lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_add_event_cb(slot_container, prv_box_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_slots[i] = slot_container;

        lv_obj_t *label = lv_label_create(content_container);
        lv_label_set_text(label, (i == 0) ? s_cart0_title : app_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_pos(label, box_x, 45);
        lv_obj_set_width(label, BOX_WIDTH);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        s_slot_labels[i] = label;
    }

    lv_obj_clear_flag(s_slot_labels[0], LV_OBJ_FLAG_HIDDEN);
}

static void prv_create_circle_area(lv_obj_t *parent)
{
    const int diameter    = CIRCLE_RADIUS * 2;
    const int total_width = DESIGN_CIRCLE_COUNT * diameter
                            + (DESIGN_CIRCLE_COUNT - 1) * CIRCLE_SPACING;
    const int start_x     = (SCREEN_W - total_width) / 2;

    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        const int cx = start_x + i * (diameter + CIRCLE_SPACING);

        lv_obj_t *circle = lv_obj_create(parent);
        lv_obj_set_size(circle, diameter, diameter);
        lv_obj_set_pos(circle, cx, CIRCLE_Y);
        lv_obj_set_style_radius(circle, CIRCLE_RADIUS, 0);
        lv_obj_set_style_bg_color(circle, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_border_width(circle, 1, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(COLOR_BLACK), 0);
        lv_obj_add_event_cb(circle, prv_circle_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_circles[i] = circle;

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, circle_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, &lv_menu_font, 0);
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

    lv_obj_set_style_pad_all(scr, 0, 0);

    int a = cart_read_title_from_sd("0:/cart.bin", s_cart0_title);
    if (a != 0) {
        strcpy(s_cart0_title, "ERR");
    }

    /*
     * ----------------------------------------------------------------
     * 图片预加载：
     *   - 槽 0：从 SD 卡读取预览图片 → SDRAM
     *   - 其他槽：从 Flash → SDRAM（DMA2D M2M，CPU 不参与数据搬运）
     *
     * 对于每个有图片的槽：
     *   1. 用 DMA2D 把像素数据搬到 SDRAM heap 对应槽地址
     *   2. 初始化该槽的 lv_image_dsc_t，.data 直接指向 SDRAM 地址
     *
     * 完成后 LVGL 渲染时 DMA2D 读写的全是 SDRAM，
     * 不再访问 Flash，彻底消除卡顿和撕裂。
     * ----------------------------------------------------------------
     */
    // 槽 0：从 SD 卡读取预览图片
    {
        uint32_t dst = SDRAM_IMG_SLOT(0);

        int ret = cart_read_preview_from_sd("0:/cart.bin", (uint8_t*)dst, IMG_SIZE);
        if (ret == 0) {
            /* 初始化独立描述符，指向 SDRAM，magic 必须设置 */
            s_image_dsc[0].header.magic = LV_IMAGE_HEADER_MAGIC;
            s_image_dsc[0].header.cf    = LV_COLOR_FORMAT_ARGB8888;
            s_image_dsc[0].header.w     = IMG_W;
            s_image_dsc[0].header.h     = IMG_H;
            s_image_dsc[0].data_size    = IMG_SIZE;
            s_image_dsc[0].data         = (const uint8_t *)dst;  /* SDRAM 地址 */
        }
    }

    // 其他槽：从 Flash 读取
    for (int i = 1; i < DESIGN_APP_COUNT; i++) {
        if (s_slot_flash_src[i] == NULL) continue;

        uint32_t dst = SDRAM_IMG_SLOT(i);

        /* DMA2D 搬运：Flash → SDRAM */
        prv_copy_img_to_sdram(dst, s_slot_flash_src[i], IMG_SIZE);

        /* 初始化独立描述符，指向 SDRAM，magic 必须设置 */
        s_image_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_image_dsc[i].header.cf    = LV_COLOR_FORMAT_ARGB8888;
        s_image_dsc[i].header.w     = IMG_W;
        s_image_dsc[i].header.h     = IMG_H;
        s_image_dsc[i].data_size    = IMG_SIZE;
        s_image_dsc[i].data         = (const uint8_t *)dst;  /* SDRAM 地址 */
    }

    /* 主容器 */
    s_main_container = lv_obj_create(scr);
    lv_obj_set_size(s_main_container, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_main_container, 0, 0);
    lv_obj_set_style_bg_color(s_main_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_main_container, 0, 0);
    lv_obj_set_style_pad_all(s_main_container, 0, 0);
    lv_obj_clear_flag(s_main_container, LV_OBJ_FLAG_SCROLLABLE);

    prv_create_box_area(s_main_container);
    prv_create_circle_area(s_main_container);
    prv_create_divider_line(s_main_container);

    s_selected_index = 0;
}

void DesignLauncher_SetSelected(int app_index)
{
    if (app_index < 0 || app_index >= DESIGN_APP_COUNT) return;
    prv_set_selection(s_slots[app_index]);
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
    /*
     * SDRAM 图片槽是静态地址，不需要 free。
     * 如果将来需要复用这段地址，在这里清零即可：
     *   memset((void*)SDRAM_IMG_BASE, 0, DESIGN_APP_COUNT * IMG_SLOT_STRIDE);
     */
    s_selected_index = 0;
}