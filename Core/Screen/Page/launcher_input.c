#include "launcher_input.h"

#include <stddef.h>
#include <string.h>

#include "launcher_scroll.h"

static int32_t input_scroll_x(const launcher_input_t *input);

#if defined(CARTDESK_LTDC_SYNC_TRACE_ENABLE) && CARTDESK_LTDC_SYNC_TRACE_ENABLE
#define LAUNCHER_INPUT_TRACE_CAPACITY 128u
typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    int16_t screen_x;
    int16_t screen_y;
    int16_t logical_x;
    int16_t content_x;
    int8_t press_target;
    int8_t current_target;
    uint8_t pressed;
    uint8_t touch_count;
    uint8_t state;
    uint8_t drag_latched;
    uint8_t action;
    uint8_t cancel_reason;
} launcher_input_trace_sample_t;

volatile launcher_input_trace_sample_t
    g_launcher_input_trace[LAUNCHER_INPUT_TRACE_CAPACITY];
volatile uint32_t g_launcher_input_trace_write;
volatile uint32_t g_launcher_input_trace_seq;

static void trace_sample(const launcher_input_t *input,
                         const launcher_pointer_sample_t *sample,
                         uint8_t action, uint8_t cancel_reason)
{
    const uint32_t index = g_launcher_input_trace_write++ % LAUNCHER_INPUT_TRACE_CAPACITY;
    const launcher_input_target_t current =
        launcher_input_hit_test(input, sample->x, sample->y);
    volatile launcher_input_trace_sample_t *dst = &g_launcher_input_trace[index];
    const int32_t logical_x = input_scroll_x(input);
    dst->seq = ++g_launcher_input_trace_seq;
    dst->timestamp_ms = sample->timestamp_ms;
    dst->screen_x = sample->x;
    dst->screen_y = sample->y;
    dst->logical_x = (int16_t)logical_x;
    dst->content_x = (int16_t)((int32_t)sample->x + logical_x);
    dst->press_target = input->press_target.id;
    dst->current_target = current.id;
    dst->pressed = sample->pressed ? 1u : 0u;
    dst->touch_count = sample->touch_count;
    dst->state = (uint8_t)input->state;
    dst->drag_latched = input->drag_latched ? 1u : 0u;
    dst->action = action;
    dst->cancel_reason = cancel_reason;
}
#else
static void trace_sample(const launcher_input_t *input,
                         const launcher_pointer_sample_t *sample,
                         uint8_t action, uint8_t cancel_reason)
{
    (void)input;
    (void)sample;
    (void)action;
    (void)cancel_reason;
}
#endif

static bool point_in_rect(const launcher_input_rect_t *rect, int32_t x, int32_t y)
{
    return x >= rect->x1 && x <= rect->x2 && y >= rect->y1 && y <= rect->y2;
}

static launcher_input_target_t no_target(void)
{
    const launcher_input_target_t target = {LAUNCHER_INPUT_TARGET_NONE, -1};
    return target;
}

static bool same_target(launcher_input_target_t lhs, launcher_input_target_t rhs)
{
    return lhs.kind == rhs.kind && lhs.id == rhs.id;
}

static int32_t input_scroll_x(const launcher_input_t *input)
{
    return input->ops.get_scroll_x != NULL ? input->ops.get_scroll_x(input->ops.user) : 0;
}

static bool movement_reached_threshold(const launcher_input_t *input,
                                       const launcher_pointer_sample_t *sample)
{
    int32_t dx = (int32_t)sample->x - input->press_x;
    int32_t dy = (int32_t)sample->y - input->press_y;
    if(dx < 0) dx = -dx;
    if(dy < 0) dy = -dy;
    return dx >= LAUNCHER_SCROLL_DIRECTION_LIMIT || dy >= LAUNCHER_SCROLL_DIRECTION_LIMIT;
}

void launcher_input_init(launcher_input_t *input,
                         const launcher_hit_geometry_t *geometry,
                         const launcher_input_ops_t *ops)
{
    if(input == NULL) return;
    (void)memset(input, 0, sizeof(*input));
    if(geometry != NULL) input->geometry = *geometry;
    if(ops != NULL) input->ops = *ops;
    input->press_target = no_target();
}

void launcher_input_reset(launcher_input_t *input)
{
    if(input == NULL) return;
    input->state = LAUNCHER_INPUT_IDLE;
    input->press_target = no_target();
    input->scroll_candidate = false;
    input->drag_latched = false;
}

launcher_input_target_t launcher_input_hit_test(const launcher_input_t *input,
                                                int16_t screen_x,
                                                int16_t screen_y)
{
    if(input == NULL) return no_target();

    /* Fixed controls are visually above the strip and always win overlaps. */
    for(uint8_t i = 0u; i < input->geometry.button_count; ++i) {
        const launcher_input_circle_t *circle = &input->geometry.buttons[i];
        const int32_t dx = (int32_t)screen_x - circle->center_x;
        const int32_t dy = (int32_t)screen_y - circle->center_y;
        const int32_t radius = circle->radius;
        if((dx * dx + dy * dy) <= (radius * radius)) {
            const launcher_input_target_t target = {
                LAUNCHER_INPUT_TARGET_FIXED_BUTTON, (int8_t)i
            };
            return target;
        }
    }

    if(!point_in_rect(&input->geometry.viewport, screen_x, screen_y)) {
        return no_target();
    }

    const int32_t content_x = (int32_t)screen_x + input_scroll_x(input);
    const int32_t content_y = (int32_t)screen_y - input->geometry.viewport.y1;
    for(uint8_t i = 0u; i < input->geometry.slot_count; ++i) {
        if(point_in_rect(&input->geometry.slots[i], content_x, content_y)) {
            const launcher_input_target_t target = {
                LAUNCHER_INPUT_TARGET_APP_SLOT, (int8_t)i
            };
            return target;
        }
    }
    return no_target();
}

void launcher_input_process(launcher_input_t *input,
                            bool hw_pan_active,
                            const launcher_pointer_sample_t *sample)
{
    if(input == NULL || sample == NULL) return;

    if(!hw_pan_active) {
        if(input->active_last_tick && input->ops.scroll_cancel != NULL) {
            input->ops.scroll_cancel(input->ops.user);
        }
        input->active_last_tick = false;
        launcher_input_reset(input);
        trace_sample(input, sample, 0u, 1u);
        return;
    }
    if(!input->active_last_tick) {
        input->active_last_tick = true;
        /* A finger held across TO_LAUNCHER_HW_PAN never becomes a fresh tap. */
        if(sample->pressed) {
            if(input->ops.scroll_cancel != NULL) input->ops.scroll_cancel(input->ops.user);
            input->state = LAUNCHER_INPUT_CANCELLED;
            input->drag_latched = true;
            trace_sample(input, sample, 0u, 2u);
            return;
        }
    }

    if(sample->touch_count > 1u) {
        if(input->state != LAUNCHER_INPUT_CANCELLED && input->ops.scroll_cancel != NULL) {
            input->ops.scroll_cancel(input->ops.user);
        }
        input->state = LAUNCHER_INPUT_CANCELLED;
        input->drag_latched = true;
        trace_sample(input, sample, 0u, 3u);
        return;
    }

    if(input->state == LAUNCHER_INPUT_CANCELLED) {
        if(!sample->pressed && sample->touch_count == 0u) launcher_input_reset(input);
        trace_sample(input, sample, 0u, 3u);
        return;
    }

    if(sample->pressed) {
        if(input->state == LAUNCHER_INPUT_IDLE) {
            input->press_x = sample->x;
            input->press_y = sample->y;
            input->press_target = launcher_input_hit_test(input, sample->x, sample->y);
            input->scroll_candidate =
                input->press_target.kind != LAUNCHER_INPUT_TARGET_FIXED_BUTTON &&
                point_in_rect(&input->geometry.viewport, sample->x, sample->y);
            input->drag_latched = false;
            input->state = LAUNCHER_INPUT_PRESSED;
        }
        else if(!input->drag_latched && movement_reached_threshold(input, sample)) {
            input->drag_latched = true;
            input->state = LAUNCHER_INPUT_DRAGGING;
        }

        if(input->scroll_candidate && input->ops.scroll_tick != NULL) {
            input->ops.scroll_tick(true, sample->x, sample->timestamp_ms, input->ops.user);
        }
        trace_sample(input, sample, 0u, 0u);
        return;
    }

    if(input->state == LAUNCHER_INPUT_IDLE) {
        /* Continue the existing inertia/snap controller after ACTION_UP. */
        if(input->ops.scroll_tick != NULL) {
            input->ops.scroll_tick(false, sample->x, sample->timestamp_ms, input->ops.user);
        }
        trace_sample(input, sample, 0u, 0u);
        return;
    }

    if(input->scroll_candidate && input->ops.scroll_tick != NULL) {
        input->ops.scroll_tick(false, sample->x, sample->timestamp_ms, input->ops.user);
    }

    if(!input->drag_latched && sample->touch_count == 0u) {
        const launcher_input_target_t release_target =
            launcher_input_hit_test(input, sample->x, sample->y);
        if(same_target(input->press_target, release_target)) {
            if(release_target.kind == LAUNCHER_INPUT_TARGET_APP_SLOT &&
               input->ops.activate_slot != NULL) {
                trace_sample(input, sample, 1u, 0u);
                input->ops.activate_slot((uint8_t)release_target.id, input->ops.user);
            }
            else if(release_target.kind == LAUNCHER_INPUT_TARGET_FIXED_BUTTON &&
                    input->ops.activate_button != NULL) {
                trace_sample(input, sample, 2u, 0u);
                input->ops.activate_button((uint8_t)release_target.id, input->ops.user);
            }
        }
    }

    /* Reset immediately: repeated RELEASED samples cannot produce another action. */
    launcher_input_reset(input);
    trace_sample(input, sample, 0u, 0u);
}
