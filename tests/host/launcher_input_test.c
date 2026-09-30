#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "launcher_input.h"

typedef struct {
    int32_t scroll_x;
    uint32_t scroll_ticks;
    uint32_t scroll_cancels;
    int32_t last_x;
    bool was_pressed;
    uint32_t slot_actions[LAUNCHER_INPUT_MAX_SLOTS];
    uint32_t button_actions[LAUNCHER_INPUT_MAX_BUTTONS];
} fixture_t;

static int32_t get_scroll_x(void *user)
{
    return ((fixture_t *)user)->scroll_x;
}

static void scroll_tick(bool pressed, int32_t screen_x, uint32_t now_ms, void *user)
{
    fixture_t *fixture = user;
    (void)now_ms;
    ++fixture->scroll_ticks;
    if(pressed && fixture->was_pressed) fixture->scroll_x += fixture->last_x - screen_x;
    fixture->last_x = screen_x;
    fixture->was_pressed = pressed;
}

static void scroll_cancel(void *user)
{
    fixture_t *fixture = user;
    ++fixture->scroll_cancels;
    fixture->was_pressed = false;
}

static void activate_slot(uint8_t slot, void *user)
{
    ++((fixture_t *)user)->slot_actions[slot];
}

static void activate_button(uint8_t button, void *user)
{
    ++((fixture_t *)user)->button_actions[button];
}

static void setup(launcher_input_t *input, fixture_t *fixture)
{
    launcher_hit_geometry_t geometry;
    (void)memset(&geometry, 0, sizeof(geometry));
    (void)memset(fixture, 0, sizeof(*fixture));
    geometry.viewport = (launcher_input_rect_t){0, 26, 799, 329};
    geometry.slot_count = LAUNCHER_INPUT_MAX_SLOTS;
    for(uint8_t i = 0u; i < geometry.slot_count; ++i) {
        const int16_t x = (int16_t)(20 + (int32_t)i * 220);
        geometry.slots[i] = (launcher_input_rect_t){x, 80, (int16_t)(x + 199), 279};
    }
    geometry.button_count = LAUNCHER_INPUT_MAX_BUTTONS;
    for(uint8_t i = 0u; i < geometry.button_count; ++i) {
        geometry.buttons[i] = (launcher_input_circle_t){
            (int16_t)(240 + (int32_t)i * 80), 358, 28
        };
    }
    const launcher_input_ops_t ops = {
        get_scroll_x, scroll_tick, scroll_cancel, activate_slot, activate_button, fixture
    };
    launcher_input_init(input, &geometry, &ops);
    const launcher_pointer_sample_t released = {0, 0, false, 0u, 0u};
    launcher_input_process(input, true, &released);
}

static launcher_pointer_sample_t sample(int16_t x, int16_t y, bool pressed,
                                        uint8_t count, uint32_t time)
{
    const launcher_pointer_sample_t value = {x, y, pressed, count, time};
    return value;
}

static void process(launcher_input_t *input, launcher_pointer_sample_t value)
{
    launcher_input_process(input, true, &value);
}

static uint32_t action_total(const fixture_t *fixture)
{
    uint32_t total = 0u;
    for(uint8_t i = 0u; i < LAUNCHER_INPUT_MAX_SLOTS; ++i) total += fixture->slot_actions[i];
    for(uint8_t i = 0u; i < LAUNCHER_INPUT_MAX_BUTTONS; ++i) total += fixture->button_actions[i];
    return total;
}

static void test_fixed_buttons(void)
{
    launcher_input_t input;
    fixture_t fixture;
    setup(&input, &fixture);
    for(uint8_t i = 0u; i < LAUNCHER_INPUT_MAX_BUTTONS; ++i) {
        const int16_t cx = (int16_t)(240 + (int32_t)i * 80);
        launcher_input_target_t target = launcher_input_hit_test(&input, cx, 358);
        assert(target.kind == LAUNCHER_INPUT_TARGET_FIXED_BUTTON && target.id == (int8_t)i);
        target = launcher_input_hit_test(&input, (int16_t)(cx + 28), 358);
        assert(target.kind == LAUNCHER_INPUT_TARGET_FIXED_BUTTON);
        target = launcher_input_hit_test(&input, (int16_t)(cx + 29), 358);
        assert(target.kind == LAUNCHER_INPUT_TARGET_NONE);
    }
}

static void test_slot_mapping(void)
{
    static const int32_t positions[] = {0, 1, 20, 240, 1000, 1859, 1860};
    launcher_input_t input;
    fixture_t fixture;
    setup(&input, &fixture);
    for(size_t p = 0u; p < sizeof(positions) / sizeof(positions[0]); ++p) {
        fixture.scroll_x = positions[p];
        for(uint8_t i = 0u; i < LAUNCHER_INPUT_MAX_SLOTS; ++i) {
            const int32_t screen_x = 20 + (int32_t)i * 220 + 100 - fixture.scroll_x;
            if(screen_x >= 0 && screen_x <= 799) {
                const launcher_input_target_t target =
                    launcher_input_hit_test(&input, (int16_t)screen_x, 206);
                assert(target.kind == LAUNCHER_INPUT_TARGET_APP_SLOT);
                assert(target.id == (int8_t)i);
            }
        }
    }
    fixture.scroll_x = 0;
    assert(launcher_input_hit_test(&input, 0, 206).kind == LAUNCHER_INPUT_TARGET_NONE);
    assert(launcher_input_hit_test(&input, 799, 206).id == 3);
    fixture.scroll_x = 1860;
    assert(launcher_input_hit_test(&input, 799, 206).kind == LAUNCHER_INPUT_TARGET_NONE);
}

static void test_gesture_arbitration(void)
{
    launcher_input_t input;
    fixture_t fixture;
    setup(&input, &fixture);

    process(&input, sample(500, 358, true, 1u, 1u));
    process(&input, sample(500, 358, false, 0u, 2u));
    assert(fixture.button_actions[3] == 1u);
    process(&input, sample(500, 358, false, 0u, 3u));
    assert(fixture.button_actions[3] == 1u);

    process(&input, sample(120, 206, true, 1u, 10u));
    process(&input, sample(125, 208, false, 0u, 11u));
    assert(fixture.slot_actions[0] == 1u);

    process(&input, sample(120, 206, true, 1u, 20u));
    process(&input, sample(100, 206, true, 1u, 21u));
    process(&input, sample(100, 206, false, 0u, 22u));
    assert(fixture.slot_actions[0] == 1u);
    assert(fixture.scroll_x == 20);

    fixture.scroll_x = 0;
    process(&input, sample(120, 206, true, 1u, 30u));
    process(&input, sample(340, 206, false, 0u, 31u));
    assert(fixture.slot_actions[0] == 1u && fixture.slot_actions[1] == 0u);

    process(&input, sample(240, 358, true, 1u, 40u));
    process(&input, sample(300, 358, true, 1u, 41u));
    process(&input, sample(300, 358, false, 0u, 42u));
    assert(fixture.button_actions[0] == 0u);

    process(&input, sample(120, 206, true, 1u, 50u));
    process(&input, sample(120, 206, true, 2u, 51u));
    process(&input, sample(120, 206, false, 0u, 52u));
    assert(fixture.slot_actions[0] == 1u);
    assert(fixture.scroll_cancels >= 1u);
}

static void test_mode_switch_and_stress(void)
{
    launcher_input_t input;
    fixture_t fixture;
    setup(&input, &fixture);
    launcher_pointer_sample_t down = sample(240, 358, true, 1u, 1u);
    launcher_pointer_sample_t up = sample(240, 358, false, 0u, 2u);
    launcher_input_process(&input, false, &down);
    launcher_input_process(&input, false, &up);
    assert(action_total(&fixture) == 0u);
    process(&input, up);

    for(uint32_t i = 0u; i < 10000u; ++i) {
        down.timestamp_ms = i * 2u + 10u;
        up.timestamp_ms = down.timestamp_ms + 1u;
        process(&input, down);
        process(&input, up);
    }
    assert(fixture.button_actions[0] == 10000u);
    assert(action_total(&fixture) == 10000u);
}

int main(void)
{
    test_fixed_buttons();
    test_slot_mapping();
    test_gesture_arbitration();
    test_mode_switch_and_stress();
    puts("launcher_input_test: PASS");
    return 0;
}
