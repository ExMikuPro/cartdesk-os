#include "launcher_hw_pan.h"

#include <string.h>

#define LAUNCHER_HW_PAN_BPP 4u

int32_t launcher_hw_pan_clamp_x(const launcher_hw_pan_geometry_t *geo, int32_t scroll_x)
{
    if (geo == NULL) {
        return 0;
    }
    if (scroll_x < 0) {
        return 0;
    }
    if ((uint32_t)scroll_x > geo->scroll_max) {
        return (int32_t)geo->scroll_max;
    }
    return scroll_x;
}

uintptr_t launcher_hw_pan_source_addr(const launcher_hw_pan_geometry_t *geo, int32_t scroll_x)
{
    if (geo == NULL || geo->base_addr == (uintptr_t)0) {
        return (uintptr_t)0;
    }
    if (scroll_x < 0 || (uint32_t)scroll_x > geo->scroll_max) {
        return (uintptr_t)0;
    }

    return geo->base_addr + ((uintptr_t)scroll_x * (uintptr_t)LAUNCHER_HW_PAN_BPP);
}

bool launcher_hw_pan_geometry_is_valid(const launcher_hw_pan_geometry_t *geo)
{
    if (geo == NULL) {
        return false;
    }

    if (geo->base_addr == (uintptr_t)0 || geo->width == 0u) {
        return false;
    }

    if (geo->viewport_width == 0u || geo->viewport_width > geo->width) {
        return false;
    }

    /* stride 必须能容纳一整行逻辑像素，且不少于 32 byte 对齐后的行字节 */
    if (geo->stride_bytes < (geo->width * LAUNCHER_HW_PAN_BPP)) {
        return false;
    }

    if ((geo->stride_bytes % 32u) != 0u) {
        return false;
    }

    /* scroll_max 必须恰好为 width - viewport_width：padding 像素不参与平移 */
    return geo->scroll_max == (geo->width - geo->viewport_width);
}

static void pan_write_addr(launcher_hw_pan_t *pan, int32_t x)
{
    const uintptr_t addr = launcher_hw_pan_source_addr(&pan->geo, x);
    if (addr != (uintptr_t)0 && pan->ops.write_source_addr != NULL) {
        pan->ops.write_source_addr(addr, pan->ops.user);
        pan->applied_x = x;
    }
}

static bool pan_submit(launcher_hw_pan_t *pan, int32_t x)
{
    pan_write_addr(pan, x);
    pan->requested_x = x;

    const uint32_t generation = ltdc_reload_arm(LTDC_RELOAD_OWNER_LAUNCHER_PAN);
    if (generation == 0u) {
        /* 已有 pending：调用方必须先等完成，不能叠加请求。 */
        ++pan->vbr_rejected;
        return false;
    }

    if (pan->ops.request_vbr != NULL && pan->ops.request_vbr(pan->ops.user)) {
        pan->pending = true;
        pan->pending_generation = generation;
        ++pan->submits;
        return true;
    }

    (void)ltdc_reload_abandon(LTDC_RELOAD_OWNER_LAUNCHER_PAN);
    ++pan->vbr_rejected;
    return false;
}

void launcher_hw_pan_init(launcher_hw_pan_t *pan,
                          const launcher_hw_pan_geometry_t *geo,
                          const launcher_hw_pan_ops_t *ops,
                          int32_t initial_x)
{
    if (pan == NULL) {
        return;
    }

    (void)memset(pan, 0, sizeof(*pan));

    if (geo != NULL) {
        pan->geo = *geo;
    }
    if (ops != NULL) {
        pan->ops = *ops;
    }

    const int32_t x = launcher_hw_pan_clamp_x(&pan->geo, initial_x);
    pan->desired_x = x;
    pan->requested_x = x;
    pan->latched_x = x;
    pan->applied_x = x;
    pan->enabled = false;
}

void launcher_hw_pan_set_enabled(launcher_hw_pan_t *pan, bool enabled)
{
    if (pan == NULL) {
        return;
    }

    pan->enabled = enabled;

    if (!enabled && pan->pending) {
        (void)ltdc_reload_abandon(LTDC_RELOAD_OWNER_LAUNCHER_PAN);
        pan->pending = false;
    }
}

bool launcher_hw_pan_set_x(launcher_hw_pan_t *pan, int32_t x)
{
    if (pan == NULL) {
        return false;
    }

    const int32_t clamped = launcher_hw_pan_clamp_x(&pan->geo, x);
    pan->desired_x = clamped;

    if (!pan->enabled) {
        return false;
    }

    if (pan->pending) {
        /* latest-position-wins：只记录目标，不叠加第二个 VBR。 */
        ++pan->coalesced;
        return false;
    }

    if (clamped == pan->latched_x) {
        return false;
    }

    return pan_submit(pan, clamped);
}

bool launcher_hw_pan_tick(launcher_hw_pan_t *pan)
{
    if (pan == NULL || !pan->enabled || pan->pending) {
        return false;
    }

    if (pan->desired_x == pan->latched_x) {
        return false;
    }

    return pan_submit(pan, pan->desired_x);
}

void launcher_hw_pan_on_reload_complete(launcher_hw_pan_t *pan, uint32_t generation)
{
    if (pan == NULL || !pan->pending) {
        return;
    }

    if (generation != pan->pending_generation) {
        ++pan->generation_mismatch;
        return;
    }

    pan->latched_x = pan->requested_x;
    pan->pending = false;
    pan->pending_generation = 0u;
    ++pan->completions;
}

bool launcher_hw_pan_is_settled(const launcher_hw_pan_t *pan)
{
    return pan != NULL && !pan->pending && pan->desired_x == pan->latched_x;
}

uintptr_t launcher_hw_pan_latched_addr(const launcher_hw_pan_t *pan)
{
    if (pan == NULL) {
        return (uintptr_t)0;
    }
    return launcher_hw_pan_source_addr(&pan->geo, pan->latched_x);
}
