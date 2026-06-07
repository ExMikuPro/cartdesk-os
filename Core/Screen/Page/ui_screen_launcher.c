// ui_screen_launcher.c
// 设计稿风格启动器实现
// 图片数据直接定位在 SDRAM heap 区，DMA2D 搬运，不经过 Flash

#include "ui_screen_launcher.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32h743xx.h"
#include "cart_bin.h"
#include "cartdesk_task.h"
#include "ui_launcher_cache.h"
#include "usart.h"


/* ------------------------------------------------------------------ */
/*  SDRAM 地址布局                                                      */
/* ------------------------------------------------------------------ */

/*
 * 帧缓冲由 ltdc.c 管理，本文件只使用 launcher cache 分区。
 *
 *  0xD0000000  Layer1_FB0         (0x177000 B)
 *  0xD0177000  Layer1_FB1         (0x177000 B)
 *  0xD02EE000  Layer2_FB0         (0x177000 B)
 *  0xD1865000  ← LAUNCHER_CACHE 起始，图片缓冲从这里开始
 *
 * 每张图片 200×200×4 = 0x3E800 字节，预留 12 个槽。
 * 总占用: 12 × 0x3E800 = 0x2E6000 B ≈ 2.9 MB，绰绰有余。
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

static char s_cart0_title[CART_BIN_TITLE_BUFFER_SIZE] = "LOADING";

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
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_launcher_screen = NULL;
static lv_obj_t *s_runtime_screen = NULL;
static bool s_runtime_exit_pending = false;
static bool s_launcher_assets_loaded = false;

/*
 * 每个槽独立的 LVGL 图像描述符。
 * .data 直接指向 SDRAM 地址，LTDC/DMA2D 原生支持，零拷贝渲染。
 */
static lv_image_dsc_t s_image_dsc[DESIGN_APP_COUNT];

static int s_selected_index = 0;

static void prv_uart_write(const char *text)
{
    size_t len;

    if (text == NULL || huart1.Instance != USART1) {
        return;
    }

    len = strlen(text);
    while (len > 0u) {
        uint16_t chunk = (uint16_t)(len > 0xFFFFu ? 0xFFFFu : len);
        if (HAL_UART_Transmit(&huart1, (uint8_t *)text, chunk, 100u) != HAL_OK) {
            return;
        }
        text += chunk;
        len -= chunk;
    }
}

static void prv_uart_log_clicked_app(int index, const char *title)
{
    char buf[128];

    if (title == NULL || title[0] == '\0') {
        title = "(untitled)";
    }

    snprintf(buf, sizeof(buf), "[launcher] clicked app %d: %s\r\n", index, title);
    prv_uart_write(buf);
}

static void prv_set_status_text(const char *text)
{
    if (s_status_label == NULL) {
        return;
    }

    if (text == NULL || text[0] == '\0') {
        lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(s_status_label, text);
    lv_obj_remove_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void prv_show_launcher_screen(void)
{
    if (s_launcher_screen == NULL) {
        s_launcher_screen = lv_obj_create(NULL);
    }

    lv_screen_load(s_launcher_screen);
    if (s_runtime_screen != NULL) {
        lv_obj_delete(s_runtime_screen);
        s_runtime_screen = NULL;
    }

    DesignLauncher_Destroy();
    DesignLauncher_Create(NULL);
}

static void prv_runtime_exit_clicked_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_current_target(e);

    if (s_runtime_exit_pending) {
        return;
    }

    s_runtime_exit_pending = true;
    if (target != NULL) {
        lv_obj_add_state(target, LV_STATE_DISABLED);
    }
    Task_LUA_Stop();
}

static void prv_show_runtime_screen(void)
{
    s_runtime_exit_pending = false;

    if (s_runtime_screen != NULL) {
        lv_obj_delete(s_runtime_screen);
        s_runtime_screen = NULL;
    }

    s_runtime_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_runtime_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_pad_all(s_runtime_screen, 0, 0);

    lv_obj_t *exit_btn = lv_button_create(s_runtime_screen);
    lv_obj_set_size(exit_btn, 96, 42);
    lv_obj_align(exit_btn, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(exit_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(exit_btn, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(exit_btn, prv_runtime_exit_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(exit_btn);
    lv_label_set_text(label, "EXIT");
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_BG), 0);
    lv_obj_center(label);

    DesignLauncher_Destroy();
    lv_screen_load(s_runtime_screen);
}

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
static void prv_copy_img_to_sdram(uint32_t dst, const uint8_t *src)
{
    /* 逐行搬运，每行 CART_BIN_PREVIEW_STRIDE 字节，共 CART_BIN_PREVIEW_H 行 */
    DMA2D->CR      = 0x00000000UL;          /* M2M 模式，无色彩转换 */
    DMA2D->FGMAR   = (uint32_t)src;         /* 源：内存地址 */
    DMA2D->OMAR    = dst;                    /* 目标：SDRAM 地址 */
    DMA2D->FGOR    = 0;                      /* 源行偏移 0（连续） */
    DMA2D->OOR     = 0;                      /* 目标行偏移 0 */
    DMA2D->FGPFCCR = 0x00000000UL;          /* 源格式 ARGB8888 */
    DMA2D->OPFCCR  = 0x00000000UL;          /* 目标格式 ARGB8888 */
    /* NLR: 每行像素数 | 行数 */
    DMA2D->NLR     = (uint32_t)(CART_BIN_PREVIEW_W) | ((uint32_t)(CART_BIN_PREVIEW_H) << 16);
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
            lv_obj_remove_flag(s_slot_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = i;
            return;
        }
    }
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        if (s_circles[i] == selected_obj) {
            lv_obj_set_style_border_width(s_circles[i], 3, 0);
            lv_obj_remove_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_t *slot = lv_event_get_current_target(e);
    int clicked_index = -1;

    prv_set_selection(slot);

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        if (slot == s_slots[i]) {
            clicked_index = i;
            break;
        }
    }

    if (clicked_index >= 0) {
        const char *title = (clicked_index == 0) ? s_cart0_title : app_names[clicked_index];
        prv_uart_log_clicked_app(clicked_index, title);
    }

    if (clicked_index == 0) {
        if (Task_LUA_IsRunning()) {
            prv_set_status_text("Lua is already running");
            return;
        }
        prv_show_runtime_screen();
        Task_LUA_StartCart("0:/cart.bin");
    }
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
    lv_obj_remove_flag(box_container, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t *content_container = lv_obj_create(box_container);
    lv_obj_set_size(content_container, content_width, container_height + 60);
    lv_obj_set_style_bg_color(content_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 0, 0);
    lv_obj_set_scrollbar_mode(content_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);

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
        lv_obj_remove_flag(slot_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(slot_container, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(slot_container, LV_OBJ_FLAG_CLICKABLE);

        /*
         * 如果该槽有图片，且 DMA2D 已在 DesignLauncher_Create 里完成了
         * Flash→SDRAM 的搬运，这里直接用 SDRAM 地址作为图片数据源。
         * LVGL 渲染时 DMA2D 读 SDRAM，带宽充足，CPU 完全不参与。
         */
        if (s_image_dsc[i].data != NULL) {
            lv_obj_t *img_obj = lv_image_create(slot_container);
            lv_obj_set_size(img_obj, BOX_WIDTH, BOX_HEIGHT);
            lv_obj_center(img_obj);
            lv_image_set_src(img_obj, &s_image_dsc[i]);
            lv_obj_set_style_border_width(img_obj, 0, LV_PART_MAIN);
            lv_obj_remove_flag(img_obj, LV_OBJ_FLAG_SCROLLABLE);
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

    lv_obj_remove_flag(s_slot_labels[0], LV_OBJ_FLAG_HIDDEN);
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

static void prv_create_status_label(lv_obj_t *parent)
{
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status_label, SCREEN_W - 80);
    lv_obj_set_pos(s_status_label, 40, LINE_Y + 16);
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */
/*  公开 API                                                            */
/* ------------------------------------------------------------------ */

void Launcher_Init(void)
{
    s_launcher_screen = lv_screen_active();
    s_runtime_exit_pending = false;
    DesignLauncher_Destroy();
    DesignLauncher_Create(NULL);
}

void Launcher_Task(void)
{
    if (!s_runtime_exit_pending) {
        return;
    }
    if (Task_LUA_IsRunning()) {
        return;
    }

    prv_show_launcher_screen();
    s_runtime_exit_pending = false;
}

void DesignLauncher_Create(lv_display_t *disp)
{
    lv_obj_t *scr = (disp != NULL)
                    ? lv_display_get_screen_active(disp)
                    : lv_screen_active();

    if (s_runtime_screen == NULL) {
        s_launcher_screen = scr;
    }

    lv_obj_set_style_pad_all(scr, 0, 0);

    if (!s_launcher_assets_loaded) {
        launcher_cache_init();

        int a = cart_bin_read_title_from_sd("0:/cart.bin", s_cart0_title);
        if (a != 0) {
            strcpy(s_cart0_title, "ERR");
        }

        /*
         * ----------------------------------------------------------------
         * 图片预加载：
         *   - 槽 0：从 SD 卡读取预览图片 → SDRAM
         *   - 其他槽：从 Flash → SDRAM（DMA2D M2M，CPU 不参与数据搬运）
         *
         * 这些资源在 launcher cache 分区里，Lua 运行期间不会被释放。
         * 从 Lua 退出回 launcher 时直接复用，避免退出后立刻再次访问 SD。
         * ----------------------------------------------------------------
         */
        // 槽 0：从 SD 卡读取预览图片
        {
            uint32_t dst = (uint32_t)launcher_get_big_icon(0);

            int ret = cart_bin_read_preview_from_sd("0:/cart.bin", (uint8_t*)dst, CART_BIN_PREVIEW_SIZE);
            if (ret == 0) {
                /* 初始化独立描述符，指向 SDRAM，magic 必须设置 */
                s_image_dsc[0].header.magic = LV_IMAGE_HEADER_MAGIC;
                s_image_dsc[0].header.cf    = LV_COLOR_FORMAT_ARGB8888;
                s_image_dsc[0].header.w     = CART_BIN_PREVIEW_W;
                s_image_dsc[0].header.h     = CART_BIN_PREVIEW_H;
                s_image_dsc[0].data_size    = CART_BIN_PREVIEW_SIZE;
                s_image_dsc[0].data         = (const uint8_t *)dst;  /* SDRAM 地址 */
            }
        }

        // 其他槽：从 Flash 读取
        for (int i = 1; i < DESIGN_APP_COUNT; i++) {
            if (s_slot_flash_src[i] == NULL) continue;

            uint32_t dst = (uint32_t)launcher_get_big_icon(i);

            /* DMA2D 搬运：Flash → SDRAM */
            prv_copy_img_to_sdram(dst, s_slot_flash_src[i]);

            /* 初始化独立描述符，指向 SDRAM，magic 必须设置 */
            s_image_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
            s_image_dsc[i].header.cf    = LV_COLOR_FORMAT_ARGB8888;
            s_image_dsc[i].header.w     = CART_BIN_PREVIEW_W;
            s_image_dsc[i].header.h     = CART_BIN_PREVIEW_H;
            s_image_dsc[i].data_size    = CART_BIN_PREVIEW_SIZE;
            s_image_dsc[i].data         = (const uint8_t *)dst;  /* SDRAM 地址 */
        }

        s_launcher_assets_loaded = true;
    }

    /* 主容器 */
    s_main_container = lv_obj_create(scr);
    lv_obj_set_size(s_main_container, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_main_container, 0, 0);
    lv_obj_set_style_bg_color(s_main_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_main_container, 0, 0);
    lv_obj_set_style_pad_all(s_main_container, 0, 0);
    lv_obj_remove_flag(s_main_container, LV_OBJ_FLAG_SCROLLABLE);

    prv_create_box_area(s_main_container);
    prv_create_circle_area(s_main_container);
    prv_create_divider_line(s_main_container);
    prv_create_status_label(s_main_container);

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
    s_status_label = NULL;
    /*
     * SDRAM 图片槽是固定 launcher cache 分区，不需要 free。
     * 如果将来需要复用这段地址，在这里清零即可：
     *   memset((void*)launcher_get_big_icon(0), 0, DESIGN_APP_COUNT * CART_BIN_PREVIEW_SIZE);
     */
    s_selected_index = 0;
}
