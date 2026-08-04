// ui_screen_launcher.c
// 设计稿风格启动器实现
// 图标从游戏卡或 QFlash littlefs 读取到 SDRAM，LVGL 直接使用该缓冲区

#include "ui_screen_launcher.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32h743xx.h"
#include "cart_bin.h"
#include "cart_io_service.h"
#include "cart_log.h"
#include "cart_system_icons.h"
#include "launcher_store.h"
#include "lua_runtime_task.h"
#include "usb_sd_transfer_mode.h"
#include "launcher_action_hints.h"
#include "runtime_stats.h"
#include "perf_monitor.h"
#include "ui_font_provider.h"
#include "ui_launcher_cache.h"

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
#define SYSTEM_ICON_SIZE      40

#define LINE_Y                420
#define LINE_X                40
#define LINE_WIDTH            720

#define SCREEN_W              800
#define SCREEN_H              480

#define COLOR_BG              0xFFFFFF
#define COLOR_BLACK           0x000000
#define COLOR_CYAN            0x00FFFF

#define LAUNCHER_VISIBLE_ICON_COUNT  4u
#define CART_PROBE_START_DELAY_MS    150u
#define CART_PROBE_PERIOD_MS         1000u
#define USB_TRANSFER_ACTION_GUARD_MS 1000u

/* ------------------------------------------------------------------ */
/*  私有状态                                                            */
/* ------------------------------------------------------------------ */

typedef struct
{
    const char *label;
    cart_system_icon_id_t icon_id;
    int8_t icon_offset_x;
    int8_t icon_offset_y;
} launcher_system_entry_t;

static const launcher_system_entry_t s_system_entries[] = {
    {"相册", CART_SYSTEM_ICON_GALLERY, 0, 0},
    {"手柄", CART_SYSTEM_ICON_GAMEPAD, -1, -1},
    {"拓展", CART_SYSTEM_ICON_EXTENSIONS, 2, -2},
    {"设置", CART_SYSTEM_ICON_SETTINGS, -1, -1},
    {"休眠模式", CART_SYSTEM_ICON_SLEEP, 0, 0},
};

_Static_assert((sizeof(s_system_entries) / sizeof(s_system_entries[0])) == DESIGN_CIRCLE_COUNT,
               "Launcher system entry count must match DESIGN_CIRCLE_COUNT");

static LauncherStoredApp s_apps[DESIGN_APP_COUNT];
static lv_obj_t *s_main_container = NULL;
static lv_obj_t *s_slots[DESIGN_APP_COUNT];
static lv_obj_t *s_slot_labels[DESIGN_APP_COUNT];
static lv_obj_t *s_slot_images[DESIGN_APP_COUNT];
static lv_obj_t *s_circles[DESIGN_CIRCLE_COUNT];
static lv_obj_t *s_circle_icons[DESIGN_CIRCLE_COUNT];
static lv_obj_t *s_circle_labels[DESIGN_CIRCLE_COUNT];
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_info_popup = NULL;
static lv_obj_t *s_launcher_screen = NULL;
static lv_obj_t *s_runtime_screen = NULL;
static bool s_runtime_exit_pending = false;
static bool s_launcher_assets_initialized = false;
static uint8_t s_cached_icon_cursor = 0u;
static bool s_cart_present = false;
static int s_inserted_slot = -1;
static uint32_t s_next_cart_probe_ms = 0u;
static LauncherActionHints s_action_hints;
static bool s_app_launch_armed = false;
static uint32_t s_usb_transfer_action_ready_ms = 0u;
static uint32_t s_io_request_id = 0u;
static cart_io_operation_t s_io_pending_operation = CART_IO_OP_NONE;
static int s_io_pending_slot = -1;
static CartBinInfo s_probe_info;

#if PERF_MONITOR_ENABLE
/*
 * GDB/OpenOCD stress-test mailbox.  The debugger only writes these scalar
 * values; all LVGL and Lua lifecycle work still runs in the normal task.
 */
volatile uint32_t g_phase3_repeat_command = 0u;
volatile uint32_t g_phase3_repeat_target = 20u;
volatile uint32_t g_phase3_repeat_dwell_loops = 400u;
volatile uint32_t g_phase3_repeat_completed = 0u;
volatile uint32_t g_phase3_repeat_failures = 0u;
volatile uint32_t g_phase3_repeat_state = 0u;
static uint32_t s_phase3_repeat_dwell_count = 0u;
#endif

/*
 * 每个槽独立的 LVGL 图像描述符。
 * .data 直接指向 SDRAM 地址，LVGL 渲染阶段无需再次复制。
 */
static lv_image_dsc_t s_image_dsc[DESIGN_APP_COUNT];

static int s_selected_index = 0;

static const char *prv_slot_title(int index)
{
    if(index < 0 || index >= DESIGN_APP_COUNT || !s_apps[index].valid) {
        return "EMPTY";
    }
    if(s_apps[index].title_zh[0] != '\0') {
        return s_apps[index].title_zh;
    }
    if(s_apps[index].title[0] != '\0') {
        return s_apps[index].title;
    }
    return "UNTITLED";
}

static LauncherActionHintState prv_make_action_hint_state(void)
{
    LauncherActionHintState state = {
        .has_selection = (s_selected_index >= 0 && s_selected_index < DESIGN_APP_COUNT),
        .can_start = false,
        .has_transfer = UsbSdTransferMode_IsAvailable(),
        .can_transfer = false,
        .transfer_active = UsbSdTransferMode_IsActive(),
        .has_info = false,
        .has_favorite_state = false,
        .is_favorite = false,
    };

    if (state.has_selection) {
        state.can_start = !state.transfer_active
                          && s_cart_present
                          && (s_selected_index == s_inserted_slot)
                          && LuaRuntimeTask_IsIdle();
        state.has_info = s_apps[s_selected_index].valid;
    }
    state.can_transfer = state.has_transfer && LuaRuntimeTask_IsIdle();

    /*
     * Detail and favorite persistence are not implemented yet.
     * Favorite state will be backed by the future KV storage layer.
     */
    return state;
}

static void prv_update_action_hints(void)
{
    LauncherActionHintState state = prv_make_action_hint_state();

    launcher_action_hints_update(&s_action_hints, &state);
}

static void prv_uart_log_clicked_app(int index, const char *title)
{
    char buf[128];

    if (title == NULL || title[0] == '\0') {
        title = "(untitled)";
    }

    snprintf(buf, sizeof(buf), "clicked app %d: %s", index, title);
    CartLog_Write(CART_LOG_INFO, "launcher", buf);
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

static const char *prv_get_selected_app_title(void)
{
    if (s_selected_index < 0 || s_selected_index >= DESIGN_APP_COUNT) {
        return "";
    }

    return prv_slot_title(s_selected_index);
}

static void prv_u64_to_dec(char *dst, uint32_t dst_size, uint64_t value)
{
    char tmp[21];
    uint32_t len = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (value == 0u) {
        dst[0] = '0';
        if (dst_size > 1u) {
            dst[1] = '\0';
        }
        return;
    }

    while (value != 0u && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    uint32_t out_len = 0;
    while (len > 0u && out_len + 1u < dst_size) {
        dst[out_len++] = tmp[--len];
    }
    dst[out_len] = '\0';
}

static void prv_format_file_size(char *dst, uint32_t dst_size, uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    uint32_t unit_index = 0;
    uint64_t unit_size = 1u;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    while (unit_index + 1u < (sizeof(units) / sizeof(units[0])) &&
           bytes >= unit_size * 1024u) {
        unit_size *= 1024u;
        unit_index++;
    }

    if (unit_index == 0u) {
        char value_text[24];

        prv_u64_to_dec(value_text, sizeof(value_text), bytes);
        snprintf(dst, dst_size, "%s %s", value_text, units[unit_index]);
        return;
    }

    uint64_t value10 = (bytes * 10u + unit_size / 2u) / unit_size;
    uint64_t whole = value10 / 10u;
    uint64_t frac = value10 % 10u;
    char whole_text[24];

    prv_u64_to_dec(whole_text, sizeof(whole_text), whole);
    if (frac == 0u) {
        snprintf(dst, dst_size, "%s %s", whole_text, units[unit_index]);
    } else {
        snprintf(dst, dst_size, "%s.%lu %s", whole_text, (unsigned long)frac, units[unit_index]);
    }
}

static bool prv_selected_app_can_start(void)
{
    return !UsbSdTransferMode_IsActive()
           && s_cart_present
           && (s_selected_index == s_inserted_slot)
           && LuaRuntimeTask_IsIdle();
}

static void prv_info_popup_close_cb(lv_event_t *e)
{
    (void)e;

    if (s_info_popup != NULL) {
        lv_obj_delete(s_info_popup);
        s_info_popup = NULL;
    }
}

static void prv_show_selected_app_info(void)
{
    char text[512];
    const char *title = prv_get_selected_app_title();
    const LauncherStoredApp *app;
    char file_size_text[24] = "0";

    if (s_main_container == NULL
        || s_selected_index < 0
        || s_selected_index >= DESIGN_APP_COUNT
        || !s_apps[s_selected_index].valid) {
        return;
    }

    app = &s_apps[s_selected_index];
    prv_format_file_size(file_size_text, sizeof(file_size_text), app->file_size);

    if (s_info_popup != NULL) {
        lv_obj_delete(s_info_popup);
        s_info_popup = NULL;
    }

    s_info_popup = lv_obj_create(s_main_container);
    lv_obj_set_size(s_info_popup, 430, 272);
    lv_obj_center(s_info_popup);
    lv_obj_set_style_bg_color(s_info_popup, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_info_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_info_popup, lv_color_hex(COLOR_BLACK), 0);
    lv_obj_set_style_border_width(s_info_popup, 2, 0);
    lv_obj_set_style_radius(s_info_popup, 4, 0);
    lv_obj_set_style_pad_all(s_info_popup, 14, 0);
    lv_obj_remove_flag(s_info_popup, LV_OBJ_FLAG_SCROLLABLE);

    snprintf(text, sizeof(text),
             "App info\nTitle: %s\nZH: %s\nPub: %s\nVer: %s\nEntry: %s\nFW: %s\nID: %08lX%08lX\nSIZE: %s\nCARD: %s",
             title,
             app->title_zh[0] != '\0' ? app->title_zh : "-",
             app->publisher[0] != '\0' ? app->publisher : "-",
             app->version[0] != '\0' ? app->version : "-",
             app->entry[0] != '\0' ? app->entry : "-",
             app->min_fw[0] != '\0' ? app->min_fw : "-",
             (unsigned long)(app->cart_id >> 32),
             (unsigned long)app->cart_id,
             file_size_text,
             (s_cart_present && s_inserted_slot == s_selected_index) ? "INSERTED" : "REQUIRED");

    lv_obj_t *label = lv_label_create(s_info_popup);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 402);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_BLACK), 0);
    lv_obj_set_style_text_font(label, UiFont_GetSystem(16u), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, 14, 12);

    lv_obj_t *close_btn = lv_button_create(s_info_popup);
    lv_obj_set_size(close_btn, 62, 34);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -10);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, prv_info_popup_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_label_set_text(btn_label, "OK");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(btn_label, UiFont_GetSystem(16u), 0);
    lv_obj_center(btn_label);
}

static void prv_show_launcher_screen(void)
{
    if (s_launcher_screen == NULL) {
        s_launcher_screen = lv_obj_create(NULL);
    }

    RuntimeStats_BeginLvglScreenOp();
    lv_screen_load(s_launcher_screen);
    if (s_runtime_screen != NULL) {
        lv_obj_delete(s_runtime_screen);
        s_runtime_screen = NULL;
    }
    RuntimeStats_EndLvglScreenOp();

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
    LuaRuntimeTask_RequestStop();
}

static void prv_show_runtime_screen(void)
{
    s_runtime_exit_pending = false;

    RuntimeStats_BeginLvglScreenOp();
    if (s_runtime_screen != NULL) {
        lv_obj_delete(s_runtime_screen);
        s_runtime_screen = NULL;
    }

    s_runtime_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_runtime_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_pad_all(s_runtime_screen, 0, 0);
    lv_obj_set_style_text_font(s_runtime_screen,
                               UiFont_GetSystem(UI_FONT_SYSTEM_DEFAULT_SIZE),
                               0);

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
    RuntimeStats_EndLvglScreenOp();
}

static void prv_configure_slot_image(int slot)
{
    if(slot < 0 || slot >= DESIGN_APP_COUNT) {
        return;
    }

    s_image_dsc[slot].header.magic = LV_IMAGE_HEADER_MAGIC;
    s_image_dsc[slot].header.cf = LV_COLOR_FORMAT_ARGB8888;
    s_image_dsc[slot].header.w = CART_BIN_PREVIEW_W;
    s_image_dsc[slot].header.h = CART_BIN_PREVIEW_H;
    s_image_dsc[slot].header.stride = CART_BIN_PREVIEW_STRIDE;
    s_image_dsc[slot].data_size = CART_BIN_PREVIEW_SIZE;
    s_image_dsc[slot].data = (const uint8_t *)launcher_get_big_icon((uint8_t)slot);
}

static void prv_attach_slot_image(int slot)
{
    if(slot < 0
       || slot >= DESIGN_APP_COUNT
       || s_slots[slot] == NULL
       || s_image_dsc[slot].data == NULL) {
        return;
    }

    if(s_slot_images[slot] == NULL) {
        s_slot_images[slot] = lv_image_create(s_slots[slot]);
        lv_obj_set_size(s_slot_images[slot], BOX_WIDTH, BOX_HEIGHT);
        lv_obj_center(s_slot_images[slot]);
        lv_obj_set_style_border_width(s_slot_images[slot], 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_slot_images[slot], LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_image_set_src(s_slot_images[slot], &s_image_dsc[slot]);
}

static void prv_update_slot_label(int slot)
{
    if(slot >= 0 && slot < DESIGN_APP_COUNT && s_slot_labels[slot] != NULL) {
        lv_label_set_text(s_slot_labels[slot], prv_slot_title(slot));
    }
}

static int prv_find_cart_slot(uint64_t cart_id)
{
    int empty_slot = -1;
    for(int index = 0; index < DESIGN_APP_COUNT; index++) {
        if(s_apps[index].valid && s_apps[index].cart_id == cart_id) {
            return index;
        }
        if(!s_apps[index].valid && empty_slot < 0) {
            empty_slot = index;
        }
    }
    return empty_slot;
}

static void prv_copy_cart_info(LauncherStoredApp *app, const CartBinInfo *info)
{
    memset(app, 0, sizeof(*app));
    app->valid = true;
    app->cart_id = info->cart_id;
    app->file_size = info->file_size;
    app->icon_size = CART_BIN_PREVIEW_SIZE;
    strncpy(app->title, info->title, sizeof(app->title) - 1u);
    strncpy(app->title_zh, info->title_zh, sizeof(app->title_zh) - 1u);
    strncpy(app->publisher, info->publisher, sizeof(app->publisher) - 1u);
    strncpy(app->version, info->version, sizeof(app->version) - 1u);
    strncpy(app->entry, info->entry, sizeof(app->entry) - 1u);
    strncpy(app->min_fw, info->min_fw, sizeof(app->min_fw) - 1u);
}

static void prv_load_cached_icons_until(uint8_t end_slot)
{
    if(end_slot > DESIGN_APP_COUNT) {
        end_slot = DESIGN_APP_COUNT;
    }

    while(s_cached_icon_cursor < end_slot && s_io_pending_operation == CART_IO_OP_NONE) {
        uint8_t slot = s_cached_icon_cursor++;
        if(!s_apps[slot].valid) {
            continue;
        }

        uint32_t *buffer = launcher_get_big_icon(slot);
        cart_io_request_t request = {
            .request_id = CartIoService_NextRequestId(),
            .operation = CART_IO_OP_LAUNCHER_STORE_READ_ICON,
        };
        request.params.launcher_read_icon.slot = slot;
        request.params.launcher_read_icon.output = (cart_task_buffer_t) {
            .data = buffer,
            .capacity = CART_BIN_PREVIEW_SIZE,
            .owner_id = 0u,
            .source = CART_BUFFER_SOURCE_CALLER,
        };
        if (CartIoService_Submit(&request, CART_IO_TIMEOUT_SD_READ_MS)) {
            s_io_request_id = request.request_id;
            s_io_pending_operation = request.operation;
            s_io_pending_slot = slot;
        }
    }
}

static void prv_load_cached_icon_step(void)
{
    if(s_cached_icon_cursor < DESIGN_APP_COUNT) {
        prv_load_cached_icons_until((uint8_t)(s_cached_icon_cursor + 1u));
    }
}

static void prv_probe_game_card(void)
{
    if (s_io_pending_operation != CART_IO_OP_NONE) {
        return;
    }

    memset(&s_probe_info, 0, sizeof(s_probe_info));
    cart_io_request_t request = {
        .request_id = CartIoService_NextRequestId(),
        .operation = CART_IO_OP_CART_PROBE,
    };
    (void)snprintf(request.params.cart.path, sizeof(request.params.cart.path),
                   "0:/cart.bin");
    request.params.cart.output = (cart_task_buffer_t) {
        .data = &s_probe_info,
        .capacity = sizeof(s_probe_info),
        .owner_id = 0u,
        .source = CART_BUFFER_SOURCE_CALLER,
    };
    if (CartIoService_Submit(&request, CART_IO_TIMEOUT_CART_HEADER_MS)) {
        s_io_request_id = request.request_id;
        s_io_pending_operation = request.operation;
        s_io_pending_slot = -1;
    }
}

static void prv_start_selected_app(void)
{
    if (!prv_selected_app_can_start()) {
        prv_set_status_text("App cannot start");
        return;
    }

    if (s_info_popup != NULL) {
        lv_obj_delete(s_info_popup);
        s_info_popup = NULL;
    }

    s_app_launch_armed = false;
    if (!LuaRuntimeTask_RequestStart("0:/cart.bin")) {
        prv_set_status_text("App cannot start");
        return;
    }
    prv_show_runtime_screen();
}

static void prv_action_hint_clicked_cb(LauncherActionHintAction action, void *user_data)
{
    (void)user_data;

    switch (action) {
    case LAUNCHER_ACTION_HINT_START:
        prv_start_selected_app();
        break;
    case LAUNCHER_ACTION_HINT_TRANSFER:
        {
            uint32_t now = HAL_GetTick();
            if ((int32_t)(now - s_usb_transfer_action_ready_ms) < 0) {
                break;
            }
            s_usb_transfer_action_ready_ms = now + USB_TRANSFER_ACTION_GUARD_MS;
        }
        if (UsbSdTransferMode_IsActive()) {
            if (UsbSdTransferMode_Exit()) {
                prv_set_status_text("USB transfer ended; scanning SD card");
                s_next_cart_probe_ms = HAL_GetTick();
            } else {
                prv_set_status_text(UsbSdTransferMode_GetLastError());
            }
        } else if (UsbSdTransferMode_Enter()) {
            s_app_launch_armed = false;
            prv_set_status_text("USB transfer active; eject on host before exit");
        } else {
            prv_set_status_text(UsbSdTransferMode_GetLastError());
        }
        prv_update_action_hints();
        break;
    case LAUNCHER_ACTION_HINT_INFO:
        prv_show_selected_app_info();
        break;
    case LAUNCHER_ACTION_HINT_BACK:
        prv_info_popup_close_cb(NULL);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  内部工具：选中状态                                                  */
/* ------------------------------------------------------------------ */

static void prv_set_selection(lv_obj_t *selected_obj)
{
    int old_selected_index = s_selected_index;

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        lv_obj_set_style_border_color(s_slots[i], lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
        lv_obj_add_flag(s_slot_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        lv_obj_set_style_border_color(s_circles[i], lv_color_hex(COLOR_BLACK), 0);
        lv_obj_set_style_outline_width(s_circles[i], 0, 0);
        if (s_circle_icons[i] != NULL) {
            lv_obj_set_style_image_recolor(s_circle_icons[i], lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
        }
        lv_obj_add_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (selected_obj == NULL) return;

    lv_obj_set_style_border_color(selected_obj, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        if (s_slots[i] == selected_obj) {
            lv_obj_remove_flag(s_slot_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = i;
            if (old_selected_index != s_selected_index) {
                s_app_launch_armed = false;
                prv_info_popup_close_cb(NULL);
            }
            prv_update_action_hints();
            return;
        }
    }
    for (int i = 0; i < DESIGN_CIRCLE_COUNT; i++) {
        if (s_circles[i] == selected_obj) {
            lv_obj_set_style_outline_width(s_circles[i], 2, 0);
            if (s_circle_icons[i] != NULL) {
                lv_obj_set_style_image_recolor(s_circle_icons[i], lv_color_hex(COLOR_CYAN), LV_PART_MAIN);
            }
            lv_obj_remove_flag(s_circle_labels[i], LV_OBJ_FLAG_HIDDEN);
            s_selected_index = -(i + 1);
            s_app_launch_armed = false;
            prv_info_popup_close_cb(NULL);
            prv_update_action_hints();
            return;
        }
    }

    prv_update_action_hints();
}

/* ------------------------------------------------------------------ */
/*  事件回调                                                            */
/* ------------------------------------------------------------------ */

static void prv_box_clicked_cb(lv_event_t *e)
{
    lv_obj_t *slot = lv_event_get_current_target(e);
    int clicked_index = -1;
    bool should_launch = false;

    for (int i = 0; i < DESIGN_APP_COUNT; i++) {
        if (slot == s_slots[i]) {
            clicked_index = i;
            break;
        }
    }

    should_launch = s_app_launch_armed
                    && (s_selected_index == clicked_index)
                    && prv_selected_app_can_start();

    prv_set_selection(slot);
    if (clicked_index >= 0) {
        s_app_launch_armed = true;
    }

    if (clicked_index >= 0) {
        prv_uart_log_clicked_app(clicked_index, prv_slot_title(clicked_index));
    }

    if (should_launch) {
        prv_start_selected_app();
    }
}

static void prv_circle_clicked_cb(lv_event_t *e)
{
    s_app_launch_armed = false;
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

        /* 缓存图标会在 Launcher_Task 中分步从 QFlash littlefs 恢复到 SDRAM。 */
        if (s_image_dsc[i].data != NULL) {
            s_slot_images[i] = lv_image_create(slot_container);
            lv_obj_set_size(s_slot_images[i], BOX_WIDTH, BOX_HEIGHT);
            lv_obj_center(s_slot_images[i]);
            lv_image_set_src(s_slot_images[i], &s_image_dsc[i]);
            lv_obj_set_style_border_width(s_slot_images[i], 0, LV_PART_MAIN);
            lv_obj_remove_flag(s_slot_images[i], LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_add_event_cb(slot_container, prv_box_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_slots[i] = slot_container;

        lv_obj_t *label = lv_label_create(content_container);
        lv_label_set_text(label, prv_slot_title(i));
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, UiFont_GetSystem(20u), 0);
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
        lv_obj_set_style_outline_width(circle, 0, 0);
        lv_obj_set_style_outline_color(circle, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_outline_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_pad(circle, 0, 0);
        lv_obj_set_style_pad_all(circle, 0, 0);
        lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(circle, prv_circle_clicked_cb, LV_EVENT_CLICKED, NULL);
        s_circles[i] = circle;

        const lv_image_dsc_t *icon_source = CartSystemIcon_GetSource(s_system_entries[i].icon_id);
        if (icon_source != NULL) {
            lv_obj_t *icon = lv_image_create(circle);
            if (icon != NULL) {
                lv_image_set_src(icon, icon_source);
                lv_obj_set_size(icon, SYSTEM_ICON_SIZE, SYSTEM_ICON_SIZE);
                lv_obj_set_pos(icon,
                               (diameter - SYSTEM_ICON_SIZE) / 2 + s_system_entries[i].icon_offset_x,
                               (diameter - SYSTEM_ICON_SIZE) / 2 + s_system_entries[i].icon_offset_y);
                lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_image_recolor(icon, lv_color_hex(COLOR_BLACK), LV_PART_MAIN);
                lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
                s_circle_icons[i] = icon;
            }
        }

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, s_system_entries[i].label);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_font(label, UiFont_GetSystem(20u), 0);
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
    lv_obj_set_style_text_font(s_status_label, UiFont_GetSystem(20u), 0);
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

static void prv_finish_cart_probe(int target_slot, uint8_t stored_slot)
{
    uint32_t *buffer = launcher_get_big_icon((uint8_t)target_slot);
    if(stored_slot != (uint8_t)target_slot && stored_slot < DESIGN_APP_COUNT) {
        memcpy(launcher_get_big_icon(stored_slot), buffer, CART_BIN_PREVIEW_SIZE);
        target_slot = stored_slot;
    }
    if(LauncherStore_Get((uint8_t)target_slot, &s_apps[target_slot]) != 0) {
        prv_copy_cart_info(&s_apps[target_slot], &s_probe_info);
    }
    prv_configure_slot_image(target_slot);
    prv_attach_slot_image(target_slot);
    prv_update_slot_label(target_slot);
    s_cart_present = true;
    s_inserted_slot = target_slot;
    s_app_launch_armed = false;
    prv_set_status_text(NULL);
    prv_update_action_hints();
}

bool Launcher_HandleIoCompletion(const cart_io_completion_t *completion)
{
    if(completion == NULL || completion->request_id != s_io_request_id ||
       completion->operation != s_io_pending_operation) {
        return false;
    }

    cart_io_operation_t operation = s_io_pending_operation;
    int pending_slot = s_io_pending_slot;
    s_io_request_id = 0u;
    s_io_pending_operation = CART_IO_OP_NONE;
    s_io_pending_slot = -1;

    if(operation == CART_IO_OP_LAUNCHER_STORE_READ_ICON) {
        if(completion->status == CART_IO_STATUS_OK && pending_slot >= 0) {
            prv_configure_slot_image(pending_slot);
            prv_attach_slot_image(pending_slot);
        }
        return true;
    }

    if(operation == CART_IO_OP_CART_PROBE) {
        if(completion->status != CART_IO_STATUS_OK) {
            if(s_cart_present) {
                s_cart_present = false;
                s_inserted_slot = -1;
                s_app_launch_armed = false;
                prv_set_status_text(NULL);
                prv_update_action_hints();
            }
            return true;
        }
        if(s_cart_present && s_inserted_slot >= 0 &&
           s_inserted_slot < DESIGN_APP_COUNT && s_apps[s_inserted_slot].valid &&
           s_apps[s_inserted_slot].cart_id == s_probe_info.cart_id) {
            return true;
        }
        int target_slot = prv_find_cart_slot(s_probe_info.cart_id);
        if(target_slot < 0) return true;

        cart_io_request_t request = {
            .request_id = CartIoService_NextRequestId(),
            .operation = CART_IO_OP_CART_READ_RESOURCE,
        };
        (void)snprintf(request.params.cart.path, sizeof(request.params.cart.path),
                       "0:/cart.bin");
        request.params.cart.output = (cart_task_buffer_t) {
            .data = launcher_get_big_icon((uint8_t)target_slot),
            .capacity = CART_BIN_PREVIEW_SIZE,
            .owner_id = 0u,
            .source = CART_BUFFER_SOURCE_CALLER,
        };
        if(CartIoService_Submit(&request, CART_IO_TIMEOUT_SD_READ_MS)) {
            s_io_request_id = request.request_id;
            s_io_pending_operation = request.operation;
            s_io_pending_slot = target_slot;
        }
        return true;
    }

    if(operation == CART_IO_OP_CART_READ_RESOURCE) {
        if(completion->status != CART_IO_STATUS_OK || pending_slot < 0) return true;
        if(!LauncherStore_IsReady()) {
            prv_finish_cart_probe(pending_slot, (uint8_t)pending_slot);
            return true;
        }
        cart_io_request_t request = {
            .request_id = CartIoService_NextRequestId(),
            .operation = CART_IO_OP_LAUNCHER_STORE_UPSERT,
        };
        request.params.launcher_upsert.info = (cart_task_buffer_t) {
            .data = &s_probe_info,
            .capacity = sizeof(s_probe_info),
            .length = sizeof(s_probe_info),
            .owner_id = 0u,
            .source = CART_BUFFER_SOURCE_CALLER,
        };
        request.params.launcher_upsert.icon = (cart_task_buffer_t) {
            .data = launcher_get_big_icon((uint8_t)pending_slot),
            .capacity = CART_BIN_PREVIEW_SIZE,
            .length = CART_BIN_PREVIEW_SIZE,
            .owner_id = 0u,
            .source = CART_BUFFER_SOURCE_CALLER,
        };
        if(CartIoService_Submit(&request, CART_IO_TIMEOUT_LFS_COMMIT_MS)) {
            s_io_request_id = request.request_id;
            s_io_pending_operation = request.operation;
            s_io_pending_slot = pending_slot;
        } else {
            prv_finish_cart_probe(pending_slot, (uint8_t)pending_slot);
        }
        return true;
    }

    if(operation == CART_IO_OP_LAUNCHER_STORE_UPSERT) {
        if(pending_slot >= 0) {
            uint8_t stored_slot = completion->status == CART_IO_STATUS_OK
                                      ? completion->result.launcher_slot
                                      : (uint8_t)pending_slot;
            prv_finish_cart_probe(pending_slot, stored_slot);
        }
        return true;
    }
    return true;
}

void Launcher_Task(void)
{
#if PERF_MONITOR_ENABLE
    if (g_phase3_repeat_command == 1u) {
        g_phase3_repeat_completed = 0u;
        g_phase3_repeat_failures = 0u;
        g_phase3_repeat_state = 1u;
        s_phase3_repeat_dwell_count = 0u;
        g_phase3_repeat_command = 0u;
    } else if (g_phase3_repeat_command == 2u) {
        g_phase3_repeat_state = 0u;
        g_phase3_repeat_command = 0u;
        if (!LuaRuntimeTask_IsIdle()) {
            s_runtime_exit_pending = true;
            LuaRuntimeTask_RequestStop();
        }
    }

    switch (g_phase3_repeat_state) {
    case 1u:
        if (LuaRuntimeTask_IsIdle() && s_main_container != NULL) {
            prv_start_selected_app();
            if (LuaRuntimeTask_IsIdle()) {
                g_phase3_repeat_failures++;
                g_phase3_repeat_state = 0u;
            } else {
                g_phase3_repeat_state = 2u;
            }
        }
        break;
    case 2u:
        if (LuaRuntimeTask_IsRunning()) {
            s_phase3_repeat_dwell_count = 0u;
            g_phase3_repeat_state = 3u;
        } else if (LuaRuntimeTask_HasError()) {
            g_phase3_repeat_failures++;
            s_runtime_exit_pending = true;
            LuaRuntimeTask_RequestStop();
            g_phase3_repeat_state = 4u;
        }
        break;
    case 3u:
        if (++s_phase3_repeat_dwell_count >= g_phase3_repeat_dwell_loops) {
            s_runtime_exit_pending = true;
            LuaRuntimeTask_RequestStop();
            g_phase3_repeat_state = 4u;
        }
        break;
    case 4u:
        if (LuaRuntimeTask_IsIdle()) {
            g_phase3_repeat_completed++;
            if (g_phase3_repeat_completed >= g_phase3_repeat_target) {
                g_phase3_repeat_state = 0u;
            } else {
                g_phase3_repeat_state = 1u;
            }
        }
        break;
    default:
        break;
    }
#endif

    if (s_main_container != NULL) {
        if(s_cached_icon_cursor < LAUNCHER_VISIBLE_ICON_COUNT) {
            prv_load_cached_icons_until(LAUNCHER_VISIBLE_ICON_COUNT);
        } else if(s_cached_icon_cursor < DESIGN_APP_COUNT) {
            prv_load_cached_icon_step();
        }

        uint32_t now = HAL_GetTick();
        if(!UsbSdTransferMode_IsActive() &&
           (int32_t)(now - s_next_cart_probe_ms) >= 0) {
            s_next_cart_probe_ms = now + CART_PROBE_PERIOD_MS;
            prv_probe_game_card();
        }
    }

    if (!s_runtime_exit_pending) {
        return;
    }
    if (!LuaRuntimeTask_IsIdle()) {
        return;
    }

    prv_show_launcher_screen();
    s_runtime_exit_pending = false;
}

void DesignLauncher_Create(lv_display_t *disp)
{
    uint32_t objects_start = PerfMonitor_Begin();
    lv_obj_t *scr = (disp != NULL)
                    ? lv_display_get_screen_active(disp)
                    : lv_screen_active();

    if (s_runtime_screen == NULL) {
        s_launcher_screen = scr;
    }

    lv_obj_set_style_pad_all(scr, 0, 0);

    if (!s_launcher_assets_initialized) {
        launcher_cache_init();
        memset(s_image_dsc, 0, sizeof(s_image_dsc));
        memset(s_apps, 0, sizeof(s_apps));
        for(uint8_t slot = 0u; slot < DESIGN_APP_COUNT; slot++) {
            (void)LauncherStore_Get(slot, &s_apps[slot]);
        }
        s_cached_icon_cursor = 0u;
        s_next_cart_probe_ms = HAL_GetTick() + CART_PROBE_START_DELAY_MS;
        s_launcher_assets_initialized = true;
    }
    memset(s_slot_images, 0, sizeof(s_slot_images));

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
    launcher_action_hints_init(&s_action_hints, s_main_container);
    launcher_action_hints_set_callback(&s_action_hints, prv_action_hint_clicked_cb, NULL);

    s_selected_index = 0;
    s_app_launch_armed = false;
    prv_update_action_hints();
    PerfMonitor_End(PERF_MONITOR_STARTUP_LAUNCHER_OBJECTS, objects_start);
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
    launcher_action_hints_deinit(&s_action_hints);

    if (s_main_container != NULL) {
        lv_obj_delete(s_main_container);
        s_main_container = NULL;
    }
    s_status_label = NULL;
    s_info_popup = NULL;
    memset(s_circle_icons, 0, sizeof(s_circle_icons));
    /*
     * SDRAM 图片槽是固定 launcher cache 分区，不需要 free。
     * 如果将来需要复用这段地址，在这里清零即可：
     *   memset((void*)launcher_get_big_icon(0), 0, DESIGN_APP_COUNT * CART_BIN_PREVIEW_SIZE);
     */
    s_selected_index = 0;
    s_app_launch_armed = false;
}
