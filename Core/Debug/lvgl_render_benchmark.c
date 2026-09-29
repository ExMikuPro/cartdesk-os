#include "lvgl_render_benchmark.h"

#if CARTDESK_RENDER_AUDIT_ENABLE

#include <string.h>

#include "display_trace.h"
#include "lvgl.h"
#include "ui_launcher_cache.h"

#define BENCH_STEP_COUNT 12u
#define BENCH_IMAGE_COUNT 4u
#define BENCH_SLOT_COUNT 5u
#define BENCH_CARD_W 200
#define BENCH_CARD_H 200
#define BENCH_CARD_GAP 20
#define BENCH_CARD_Y 106
#define BENCH_ALPHA_W 64u
#define BENCH_ALPHA_H 64u

volatile uint32_t g_lvgl_render_bench_command;
volatile uint32_t g_lvgl_render_bench_run;
volatile uint32_t g_lvgl_render_bench_state;
volatile uint32_t g_lvgl_render_bench_scene;
volatile uint32_t g_lvgl_render_bench_step;

static lv_obj_t *s_screen;
static lv_obj_t *s_moving_root;
static lv_image_dsc_t s_images[BENCH_IMAGE_COUNT];
static lv_image_dsc_t s_alpha_image;
static uint32_t s_alpha_pixels[BENCH_ALPHA_W * BENCH_ALPHA_H]
    __attribute__((section(".ram_runtime"), aligned(32)));
static uint32_t s_last_render_count;

static void style_plain(lv_obj_t *obj, uint32_t color)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
}

static void create_scene(uint32_t scene)
{
    s_screen = lv_obj_create(NULL);
    if(s_screen == NULL) {
        g_lvgl_render_bench_state = LVGL_RENDER_BENCH_STATE_ERROR;
        return;
    }
    style_plain(s_screen, 0x06131Au);

    if(scene >= LVGL_RENDER_BENCH_COMMAND_L2) {
        s_moving_root = lv_obj_create(s_screen);
        if(s_moving_root == NULL) {
            g_lvgl_render_bench_state = LVGL_RENDER_BENCH_STATE_ERROR;
            return;
        }
        style_plain(s_moving_root, 0x0A2028u);
        lv_obj_set_pos(s_moving_root, 0, 26);
        lv_obj_set_size(s_moving_root, 1100, 350);
    }

    if(scene == LVGL_RENDER_BENCH_COMMAND_L1) {
        s_moving_root = lv_obj_create(s_screen);
        style_plain(s_moving_root, 0x18D7E8u);
        lv_obj_set_pos(s_moving_root, 0, BENCH_CARD_Y);
        lv_obj_set_size(s_moving_root, BENCH_CARD_W, BENCH_CARD_H);
    }
    else if(scene == LVGL_RENDER_BENCH_COMMAND_L2) {
        for(uint32_t i = 0u; i < BENCH_SLOT_COUNT; ++i) {
            lv_obj_t *rect = lv_obj_create(s_moving_root);
            style_plain(rect, 0x18313Au + i * 0x080808u);
            lv_obj_set_pos(rect, (int32_t)i * (BENCH_CARD_W + BENCH_CARD_GAP), BENCH_CARD_Y - 26);
            lv_obj_set_size(rect, BENCH_CARD_W, BENCH_CARD_H);
        }
    }
    else if(scene == LVGL_RENDER_BENCH_COMMAND_L3_XRGB ||
            scene == LVGL_RENDER_BENCH_COMMAND_L4_ARGB) {
        for(uint32_t i = 0u; i < BENCH_IMAGE_COUNT; ++i) {
            memset(&s_images[i], 0, sizeof(s_images[i]));
            s_images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
            s_images[i].header.cf = scene == LVGL_RENDER_BENCH_COMMAND_L3_XRGB
                                      ? LV_COLOR_FORMAT_XRGB8888
                                      : LV_COLOR_FORMAT_ARGB8888;
            s_images[i].header.w = BENCH_CARD_W;
            s_images[i].header.h = BENCH_CARD_H;
            s_images[i].header.stride = BENCH_CARD_W * 4u;
            s_images[i].data_size = BENCH_CARD_W * BENCH_CARD_H * 4u;
            s_images[i].data = (const uint8_t *)launcher_get_big_icon((uint8_t)i);
            lv_obj_t *image = lv_image_create(s_moving_root);
            lv_image_set_src(image, &s_images[i]);
            lv_obj_set_pos(image, (int32_t)i * (BENCH_CARD_W + BENCH_CARD_GAP), BENCH_CARD_Y - 26);
            lv_obj_set_size(image, BENCH_CARD_W, BENCH_CARD_H);
        }
    }
    else if(scene == LVGL_RENDER_BENCH_COMMAND_L5_ALPHA) {
        for(uint32_t y = 0u; y < BENCH_ALPHA_H; ++y) {
            for(uint32_t x = 0u; x < BENCH_ALPHA_W; ++x) {
                uint32_t alpha = 32u + ((x + y) * 191u) / (BENCH_ALPHA_W + BENCH_ALPHA_H - 2u);
                s_alpha_pixels[y * BENCH_ALPHA_W + x] =
                    (alpha << 24) | (UINT32_C(0x35) << 16) | (UINT32_C(0xD9) << 8) | UINT32_C(0xF2);
            }
        }
        memset(&s_alpha_image, 0, sizeof(s_alpha_image));
        s_alpha_image.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_alpha_image.header.cf = LV_COLOR_FORMAT_ARGB8888;
        s_alpha_image.header.w = BENCH_ALPHA_W;
        s_alpha_image.header.h = BENCH_ALPHA_H;
        s_alpha_image.header.stride = BENCH_ALPHA_W * 4u;
        s_alpha_image.data_size = sizeof(s_alpha_pixels);
        s_alpha_image.data = (const uint8_t *)s_alpha_pixels;
        lv_obj_t *image = lv_image_create(s_moving_root);
        lv_image_set_src(image, &s_alpha_image);
        lv_obj_set_pos(image, 120, 80);
        lv_obj_set_size(image, BENCH_ALPHA_W, BENCH_ALPHA_H);
    }

    lv_screen_load(s_screen);
    g_lvgl_render_bench_scene = scene;
    g_lvgl_render_bench_step = 0u;
    g_lvgl_render_bench_run = 0u;
    g_lvgl_render_bench_state = LVGL_RENDER_BENCH_STATE_READY;
}

static void advance_scene(void)
{
    if(g_lvgl_render_bench_scene == LVGL_RENDER_BENCH_COMMAND_L0) {
        lv_obj_invalidate(s_screen);
    }
    else {
        lv_obj_set_x(s_moving_root, (g_lvgl_render_bench_step & 1u) == 0u ? -20 : 0);
    }
    ++g_lvgl_render_bench_step;
    s_last_render_count = g_display_render_count;
}

bool LvglRenderBenchmark_Poll(void)
{
    uint32_t command = g_lvgl_render_bench_command;
    if(command >= LVGL_RENDER_BENCH_COMMAND_L0 && command <= LVGL_RENDER_BENCH_COMMAND_L5_ALPHA &&
       g_lvgl_render_bench_state == LVGL_RENDER_BENCH_STATE_IDLE) {
        g_lvgl_render_bench_command = LVGL_RENDER_BENCH_COMMAND_NONE;
        create_scene(command);
    }

    if(g_lvgl_render_bench_state == LVGL_RENDER_BENCH_STATE_READY && g_lvgl_render_bench_run != 0u) {
        g_lvgl_render_bench_run = 0u;
        g_lvgl_render_bench_step = 0u;
        s_last_render_count = g_display_render_count;
        g_lvgl_render_bench_state = LVGL_RENDER_BENCH_STATE_RUNNING;
        advance_scene();
    }
    else if(g_lvgl_render_bench_state == LVGL_RENDER_BENCH_STATE_RUNNING &&
            g_display_render_count != s_last_render_count) {
        if(g_lvgl_render_bench_step >= BENCH_STEP_COUNT) {
            g_lvgl_render_bench_state = LVGL_RENDER_BENCH_STATE_DONE;
        }
        else {
            advance_scene();
        }
    }

    return g_lvgl_render_bench_state != LVGL_RENDER_BENCH_STATE_IDLE;
}

#endif
