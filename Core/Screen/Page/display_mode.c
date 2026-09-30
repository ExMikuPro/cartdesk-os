#include "display_mode.h"

#include <string.h>

const char *display_mode_name(display_mode_t mode)
{
    switch (mode) {
    case DISPLAY_MODE_LVGL_APP:            return "LVGL_APP";
    case DISPLAY_MODE_TO_LAUNCHER_HW_PAN:  return "TO_LAUNCHER_HW_PAN";
    case DISPLAY_MODE_LAUNCHER_HW_PAN:     return "LAUNCHER_HW_PAN";
    case DISPLAY_MODE_TO_LVGL_APP:         return "TO_LVGL_APP";
    default:                               return "?";
    }
}

static bool mode_is_stable_value(display_mode_t mode)
{
    return mode == DISPLAY_MODE_LVGL_APP || mode == DISPLAY_MODE_LAUNCHER_HW_PAN;
}

void display_mode_init(display_mode_state_t *st)
{
    if (st == NULL) {
        return;
    }
    (void)memset(st, 0, sizeof(*st));
    st->mode = DISPLAY_MODE_LVGL_APP;
}

bool display_mode_is_stable(const display_mode_state_t *st)
{
    return st != NULL && mode_is_stable_value(st->mode);
}

bool display_mode_lvgl_refresh_allowed(const display_mode_state_t *st)
{
    if (st == NULL) {
        return false;
    }

    /* 只有稳定 Launcher 模式关闭了 LVGL invalidation。
     * - TO_LAUNCHER_HW_PAN：图层还只在 shadow 里，尚未 latch，LVGL 仍可刷新
     * - TO_LVGL_APP：必须允许刷新，第一张 full frame 的 flush 就是原子 latch */
    return st->mode != DISPLAY_MODE_LAUNCHER_HW_PAN;
}

bool display_mode_hw_pan_active(const display_mode_state_t *st)
{
    return st != NULL && st->mode == DISPLAY_MODE_LAUNCHER_HW_PAN;
}

display_mode_t display_mode_target(const display_mode_state_t *st)
{
    if (st == NULL) {
        return DISPLAY_MODE_LVGL_APP;
    }

    switch (st->mode) {
    case DISPLAY_MODE_TO_LAUNCHER_HW_PAN:
        return DISPLAY_MODE_LAUNCHER_HW_PAN;
    case DISPLAY_MODE_TO_LVGL_APP:
        return DISPLAY_MODE_LVGL_APP;
    default:
        return st->mode;
    }
}

bool display_mode_request(display_mode_state_t *st,
                          display_mode_t target,
                          bool reload_pending)
{
    if (st == NULL) {
        return false;
    }

    if (!mode_is_stable_value(target)) {
        ++st->rejected_invalid;
        return false;
    }

    if (!display_mode_is_stable(st)) {
        ++st->rejected_busy;
        return false;
    }

    if (reload_pending) {
        ++st->rejected_reload;
        return false;
    }

    if (st->mode == target) {
        /* 已经在目标模式：无需过渡，报告成功但不产生 TO_* 中间态。 */
        return true;
    }

    st->mode = (target == DISPLAY_MODE_LAUNCHER_HW_PAN)
                   ? DISPLAY_MODE_TO_LAUNCHER_HW_PAN
                   : DISPLAY_MODE_TO_LVGL_APP;
    ++st->request_count;
    return true;
}

bool display_mode_commit(display_mode_state_t *st)
{
    if (st == NULL || display_mode_is_stable(st)) {
        return false;
    }

    st->mode = display_mode_target(st);
    ++st->commit_count;
    return true;
}

bool display_mode_abort(display_mode_state_t *st, display_mode_t from)
{
    if (st == NULL || display_mode_is_stable(st)) {
        return false;
    }

    if (!mode_is_stable_value(from)) {
        return false;
    }

    st->mode = from;
    ++st->abort_count;
    return true;
}
