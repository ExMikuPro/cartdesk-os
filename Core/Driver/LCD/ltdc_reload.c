#include "ltdc_reload.h"

#include <string.h>

static ltdc_reload_state_t s_reload;
static ltdc_reload_external_cb_t s_external_cb;
static void *s_external_user;
static uint32_t s_unhandled_external;

static bool owner_is_valid(ltdc_reload_owner_t owner)
{
    return owner > LTDC_RELOAD_OWNER_NONE && owner < LTDC_RELOAD_OWNER_COUNT;
}

const char *ltdc_reload_owner_name(ltdc_reload_owner_t owner)
{
    switch (owner) {
    case LTDC_RELOAD_OWNER_NONE:          return "NONE";
    case LTDC_RELOAD_OWNER_LVGL_FLUSH:    return "LVGL_FLUSH";
    case LTDC_RELOAD_OWNER_LAUNCHER_PAN:  return "LAUNCHER_PAN";
    case LTDC_RELOAD_OWNER_LAYER0_SWAP:   return "LAYER0_SWAP";
    case LTDC_RELOAD_OWNER_MODE_SWITCH:   return "MODE_SWITCH";
    default:                              return "?";
    }
}

void ltdc_reload_init(void)
{
    (void)memset(&s_reload, 0, sizeof(s_reload));
    s_reload.initialized = true;
    s_external_cb = NULL;
    s_external_user = NULL;
    s_unhandled_external = 0u;
}

void ltdc_reload_set_external_dispatch(ltdc_reload_external_cb_t cb, void *user)
{
    s_external_cb = cb;
    s_external_user = user;
}

bool ltdc_reload_notify_external(ltdc_reload_owner_t owner, uint32_t generation)
{
    if (owner == LTDC_RELOAD_OWNER_NONE) {
        return false;
    }

    if (s_external_cb == NULL) {
        /* 启动早期的配置 latch（LCD_DoubleBufferInit）发生在上层注册消费者
         * 之前，这是预期情况而不是错误；只有"完全没有 owner"才算异常。 */
        ++s_unhandled_external;
        return false;
    }

    s_external_cb(owner, generation, s_external_user);
    return true;
}

uint32_t ltdc_reload_unhandled_external(void)
{
    return s_unhandled_external;
}

uint32_t ltdc_reload_arm(ltdc_reload_owner_t owner)
{
    if (!owner_is_valid(owner)) {
        return 0u;
    }

    if (s_reload.pending_owner != LTDC_RELOAD_OWNER_NONE) {
        /* 上一笔尚未完成：调用方必须先等待或放弃，这里只记录异常。 */
        ++s_reload.overrun_count;
        return 0u;
    }

    ++s_reload.pending_generation;
    if (s_reload.pending_generation == 0u) {
        /* generation 0 是"无请求"哨兵，跳过它。 */
        ++s_reload.pending_generation;
    }

    s_reload.pending_owner = owner;
    ++s_reload.arming_count;
    ++s_reload.owner_arms[owner];
    return s_reload.pending_generation;
}

ltdc_reload_owner_t ltdc_reload_complete_from_irq(void)
{
    const ltdc_reload_owner_t owner = s_reload.pending_owner;

    if (owner == LTDC_RELOAD_OWNER_NONE) {
        /* 没有登记过的 VBR（例如 LCD_DoubleBufferInit 的 legacy 请求）。 */
        ++s_reload.orphan_completions;
        return LTDC_RELOAD_OWNER_NONE;
    }

    s_reload.last_owner = owner;
    s_reload.last_generation = s_reload.pending_generation;
    s_reload.pending_owner = LTDC_RELOAD_OWNER_NONE;
    ++s_reload.completion_count;
    ++s_reload.owner_completions[owner];
    return owner;
}

bool ltdc_reload_abandon(ltdc_reload_owner_t owner)
{
    if (!owner_is_valid(owner)) {
        return false;
    }

    if (s_reload.pending_owner == LTDC_RELOAD_OWNER_NONE) {
        return false;
    }

    if (s_reload.pending_owner != owner) {
        ++s_reload.crosstalk_count;
        return false;
    }

    s_reload.pending_owner = LTDC_RELOAD_OWNER_NONE;
    return true;
}

bool ltdc_reload_is_pending(void)
{
    return s_reload.pending_owner != LTDC_RELOAD_OWNER_NONE;
}

ltdc_reload_owner_t ltdc_reload_pending_owner(void)
{
    return s_reload.pending_owner;
}

uint32_t ltdc_reload_pending_generation(void)
{
    return (s_reload.pending_owner == LTDC_RELOAD_OWNER_NONE)
               ? 0u
               : s_reload.pending_generation;
}

const ltdc_reload_state_t *ltdc_reload_state(void)
{
    return &s_reload;
}

bool ltdc_reload_is_consistent(void)
{
    return s_reload.orphan_completions == 0u &&
           s_reload.overrun_count == 0u &&
           s_reload.crosstalk_count == 0u;
}
