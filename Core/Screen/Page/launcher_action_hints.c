// launcher_action_hints.c
// Switch-style action hints for the native launcher UI.

#include "launcher_action_hints.h"

#include <stddef.h>
#include <string.h>

extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;

#define ACTION_HINT_MARGIN_RIGHT   14
#define ACTION_HINT_MARGIN_BOTTOM  10
#define ACTION_HINT_BAR_HEIGHT     38
#define ACTION_HINT_BAR_PAD_X      8
#define ACTION_HINT_BAR_PAD_Y      6
#define ACTION_HINT_ITEM_GAP       10
#define ACTION_HINT_TEXT_COLOR     0x111111
#define ACTION_HINT_DISABLED_COLOR 0x9AA0A6
#define ACTION_HINT_MAX_BAR_WIDTH  360

typedef struct {
    LauncherActionHintAction action;
    const char *text;
    bool enabled;
} LauncherHintItemModel;

static void prv_reset(LauncherActionHints *hints)
{
    if (hints == NULL) {
        return;
    }

    memset(hints, 0, sizeof(*hints));
}

static uint16_t prv_utf8_char_count(const char *text)
{
    uint16_t count = 0;

    if (text == NULL) {
        return 0;
    }

    while (*text != '\0') {
        if (((uint8_t)*text & 0xC0u) != 0x80u) {
            ++count;
        }
        ++text;
    }

    return count;
}

static uint16_t prv_item_width(const char *text)
{
    uint16_t chars = prv_utf8_char_count(text);

    return (uint16_t)(chars * 14 + 12);
}

static uint16_t prv_layout_items(LauncherActionHints *hints,
                                 const LauncherHintItemModel *items,
                                 uint8_t count)
{
    uint16_t x = ACTION_HINT_BAR_PAD_X;

    for (uint8_t i = 0; i < LAUNCHER_ACTION_HINT_MAX_ITEMS; ++i) {
        if (hints->items[i] != NULL) {
            lv_obj_add_flag(hints->items[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t width = prv_item_width(items[i].text);

        if (hints->items[i] == NULL || hints->labels[i] == NULL) {
            continue;
        }

        lv_obj_set_size(hints->items[i], width, ACTION_HINT_BAR_HEIGHT - ACTION_HINT_BAR_PAD_Y * 2);
        lv_obj_set_pos(hints->items[i], x, ACTION_HINT_BAR_PAD_Y);
        if (items[i].enabled) {
            lv_obj_add_flag(hints->items[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_remove_flag(hints->items[i], LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_remove_state(hints->items[i], LV_STATE_DISABLED);
        if (!items[i].enabled) {
            lv_obj_add_state(hints->items[i], LV_STATE_DISABLED);
        }
        lv_obj_remove_flag(hints->items[i], LV_OBJ_FLAG_HIDDEN);
        hints->actions[i] = items[i].action;
        hints->enabled[i] = items[i].enabled;

        lv_label_set_text(hints->labels[i], items[i].text);
        lv_obj_set_width(hints->labels[i], (int32_t)width);
        lv_obj_set_pos(hints->labels[i], 0, 2);
        lv_obj_set_style_text_color(hints->labels[i],
                                    lv_color_hex(items[i].enabled
                                                 ? ACTION_HINT_TEXT_COLOR
                                                 : ACTION_HINT_DISABLED_COLOR), 0);
        lv_obj_set_style_text_opa(hints->labels[i], items[i].enabled ? LV_OPA_COVER : LV_OPA_50, 0);

        x = (uint16_t)(x + width + ACTION_HINT_ITEM_GAP);
    }

    return (count == 0u) ? 0u : (uint16_t)(x - ACTION_HINT_ITEM_GAP + ACTION_HINT_BAR_PAD_X);
}

static uint16_t prv_total_width(const LauncherHintItemModel *items, uint8_t count)
{
    uint16_t width = ACTION_HINT_BAR_PAD_X * 2;

    for (uint8_t i = 0; i < count; ++i) {
        width = (uint16_t)(width + prv_item_width(items[i].text));
        if (i + 1u < count) {
            width = (uint16_t)(width + ACTION_HINT_ITEM_GAP);
        }
    }

    return width;
}

static uint8_t prv_make_items(const LauncherActionHintState *state,
                              LauncherHintItemModel *items,
                              uint8_t max_items)
{
    uint8_t count = 0;

    if (state == NULL || max_items < 1u) {
        return 0;
    }

    if (state->has_selection && count < max_items) {
        items[count++] = (LauncherHintItemModel) {
            .action = LAUNCHER_ACTION_HINT_START,
            .text = "A 起动",
            .enabled = state->can_start,
        };
    }

    if (count < max_items) {
        items[count++] = (LauncherHintItemModel) {
            .action = LAUNCHER_ACTION_HINT_BACK,
            .text = "B 返回",
            .enabled = true,
        };
    }

    if (state->has_selection && count < max_items) {
        items[count++] = (LauncherHintItemModel) {
            .action = LAUNCHER_ACTION_HINT_INFO,
            .text = "X 信息",
            .enabled = state->has_info,
        };
    }

    (void)state->has_favorite_state;
    (void)state->is_favorite;

    return count;
}

static int prv_find_item_index(const LauncherActionHints *hints, lv_obj_t *item)
{
    if (hints == NULL || item == NULL) {
        return -1;
    }

    for (uint8_t i = 0; i < LAUNCHER_ACTION_HINT_MAX_ITEMS; ++i) {
        if (hints->items[i] == item) {
            return (int)i;
        }
    }

    return -1;
}

static void prv_item_clicked_cb(lv_event_t *e)
{
    LauncherActionHints *hints = (LauncherActionHints *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_current_target(e);
    int index = prv_find_item_index(hints, target);

    if (hints == NULL || hints->callback == NULL || index < 0) {
        return;
    }

    if (!hints->enabled[index]) {
        return;
    }

    hints->callback(hints->actions[index], hints->callback_user_data);
}

void launcher_action_hints_init(LauncherActionHints *hints, lv_obj_t *parent)
{
    if (hints == NULL) {
        return;
    }

    prv_reset(hints);

    if (parent == NULL) {
        return;
    }

    hints->root = lv_obj_create(parent);
    if (hints->root == NULL) {
        return;
    }

    lv_obj_set_size(hints->root, 1, ACTION_HINT_BAR_HEIGHT);
    lv_obj_align(hints->root, LV_ALIGN_BOTTOM_RIGHT, -ACTION_HINT_MARGIN_RIGHT, -ACTION_HINT_MARGIN_BOTTOM);
    lv_obj_set_style_bg_opa(hints->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hints->root, 0, 0);
    lv_obj_set_style_radius(hints->root, 0, 0);
    lv_obj_set_style_pad_all(hints->root, 0, 0);
    lv_obj_remove_flag(hints->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hints->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(hints->root, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(hints->root, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < LAUNCHER_ACTION_HINT_MAX_ITEMS; ++i) {
        hints->items[i] = lv_obj_create(hints->root);
        if (hints->items[i] == NULL) {
            continue;
        }

        lv_obj_set_style_bg_opa(hints->items[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hints->items[i], 0, 0);
        lv_obj_set_style_pad_all(hints->items[i], 0, 0);
        lv_obj_remove_flag(hints->items[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(hints->items[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(hints->items[i], LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_event_cb(hints->items[i], prv_item_clicked_cb, LV_EVENT_CLICKED, hints);
        lv_obj_add_flag(hints->items[i], LV_OBJ_FLAG_HIDDEN);

        hints->labels[i] = lv_label_create(hints->items[i]);

        if (hints->labels[i] != NULL) {
            lv_obj_set_style_text_font(hints->labels[i], &lv_font_source_han_sans_sc_16_cjk, 0);
            lv_label_set_long_mode(hints->labels[i], LV_LABEL_LONG_CLIP);
            lv_obj_remove_flag(hints->labels[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(hints->labels[i], LV_OBJ_FLAG_CLICK_FOCUSABLE);
        }
    }
}

void launcher_action_hints_deinit(LauncherActionHints *hints)
{
    if (hints == NULL) {
        return;
    }

    if (hints->root != NULL) {
        lv_obj_delete(hints->root);
    }

    prv_reset(hints);
}

void launcher_action_hints_set_visible(LauncherActionHints *hints, bool visible)
{
    if (hints == NULL || hints->root == NULL) {
        return;
    }

    hints->visible = visible;
    if (visible) {
        lv_obj_remove_flag(hints->root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(hints->root, LV_OBJ_FLAG_HIDDEN);
    }
}

void launcher_action_hints_update(LauncherActionHints *hints, const LauncherActionHintState *state)
{
    LauncherHintItemModel items[LAUNCHER_ACTION_HINT_MAX_ITEMS];
    uint8_t count;
    uint16_t width;

    if (hints == NULL || hints->root == NULL || state == NULL) {
        return;
    }

    count = prv_make_items(state, items, LAUNCHER_ACTION_HINT_MAX_ITEMS);
    while (count > 2u && prv_total_width(items, count) > ACTION_HINT_MAX_BAR_WIDTH) {
        --count;
    }

    if (count == 0u) {
        hints->count = 0;
        launcher_action_hints_set_visible(hints, false);
        return;
    }

    width = prv_layout_items(hints, items, count);
    lv_obj_set_size(hints->root, width, ACTION_HINT_BAR_HEIGHT);
    lv_obj_align(hints->root, LV_ALIGN_BOTTOM_RIGHT, -ACTION_HINT_MARGIN_RIGHT, -ACTION_HINT_MARGIN_BOTTOM);

    hints->count = count;
    launcher_action_hints_set_visible(hints, true);
}

void launcher_action_hints_set_callback(LauncherActionHints *hints,
                                        LauncherActionHintCallback callback,
                                        void *user_data)
{
    if (hints == NULL) {
        return;
    }

    hints->callback = callback;
    hints->callback_user_data = user_data;
}
