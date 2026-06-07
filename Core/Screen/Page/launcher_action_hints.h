// launcher_action_hints.h
// Native LVGL action hint bar for the launcher.
#ifndef LAUNCHER_ACTION_HINTS_H
#define LAUNCHER_ACTION_HINTS_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define LAUNCHER_ACTION_HINT_MAX_ITEMS 5

typedef struct {
    bool has_selection;
    bool can_start;
    bool has_info;
    bool has_favorite_state;
    bool is_favorite;
} LauncherActionHintState;

typedef enum {
    LAUNCHER_ACTION_HINT_START,
    LAUNCHER_ACTION_HINT_BACK,
    LAUNCHER_ACTION_HINT_INFO,
} LauncherActionHintAction;

typedef void (*LauncherActionHintCallback)(LauncherActionHintAction action, void *user_data);

typedef struct {
    lv_obj_t *root;
    lv_obj_t *items[LAUNCHER_ACTION_HINT_MAX_ITEMS];
    lv_obj_t *labels[LAUNCHER_ACTION_HINT_MAX_ITEMS];
    LauncherActionHintAction actions[LAUNCHER_ACTION_HINT_MAX_ITEMS];
    bool enabled[LAUNCHER_ACTION_HINT_MAX_ITEMS];
    LauncherActionHintCallback callback;
    void *callback_user_data;
    uint8_t count;
    bool visible;
} LauncherActionHints;

void launcher_action_hints_init(LauncherActionHints *hints, lv_obj_t *parent);
void launcher_action_hints_deinit(LauncherActionHints *hints);
void launcher_action_hints_set_visible(LauncherActionHints *hints, bool visible);
void launcher_action_hints_update(LauncherActionHints *hints, const LauncherActionHintState *state);
void launcher_action_hints_set_callback(LauncherActionHints *hints,
                                        LauncherActionHintCallback callback,
                                        void *user_data);

#endif // LAUNCHER_ACTION_HINTS_H
